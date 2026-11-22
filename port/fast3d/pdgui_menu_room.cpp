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
#include "net/lobby_view.h"
#include "net/grid_edit_playtest_view.h"
#include "net/grid_edit_room_net.h"
#include "net/grid_match_result.h"
#include "net/grid_mod_proposal.h"
#include "forge_level_io.h"
#include "forge_room_builder.h"
#include "grid_match_package_io.h"
#include "grid_match_runtime.h"
#include <float.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <algorithm>
#include <cmath>
#include <vector>

#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include "pdgui_friends.h"
#include "pdgui_hotswap.h"
#include "pdgui_style.h"
#include "pdgui_scaling.h"
#include "pdgui_audio.h"
#include "pdgui_nav_input.h"
#include "pdgui_pausemenu.h"
#include "pdgui_layout.h"       /* pdguiPopupDarkenBehind */
#include "pdgui_widgets.h"      /* Priority L: shared label-left widget helpers */
#include "pdgui_grid_variant_picker.h"
#include "system.h"
#include "inputctx.h"
#include "menupool.h"
#include "menugraph.h"
#include "room.h"
#include "combat_sim_verify.h"

/* ========================================================================
 * Forward declarations (C boundary)
 * ======================================================================== */

extern "C" {

#include "assetcatalog.h"
#include "asset_runtime.h"
#include "botvariant.h"
#include "pdgui_character_portrait.h"
#include "pdgui_charpreview.h"
#include "pdgui_nav.h"
#include "game/forgemode.h"
char *langGet(s32 textid);
char *langSafe(s32 textid);
s32 challengeIsFeatureUnlocked(u32 feature);
/* The original's preset games (mplayer.c), for the Room's Load Preset. The
 * names come without the language file's line terminator (B-1295). */
s32 mpGetNumUnlockedPresets(void);
void mpCopyPresetNameBySlot(s32 slot, char *out, s32 size);

/* Rule 6 reconciliation (Q4, 2026-05-03): Y-Social is restricted to
 * Main Menu + Pause Menu only. Combat Sim does NOT bind Y to social.
 * Forward declarations of pdguiFriendsSocialOpen / IsOpen previously
 * lived here; removed after the Y-Social handler at pdguiRoomScreenRender
 * was deleted per Mike's Q4 inversion. The Y-Social rollout on
 * Main Menu and Pause Menu is tracked in kanban c087 + c088. The
 * c086 removed the old per-row Y multi-select reuse too, so Y remains
 * undefined on this screen. */

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
s32 netmsgGridCanProposeMod(void);
s32 netmsgGridProposeMod(const char *id);
const grid_mod_proposal_control_t *netmsgGridReviewModProposal(void);
s32 netmsgGridResolveModProposal(s32 accept);
const char *netmsgGridModProposalNotice(void);
void pdguiRoomClearPlayedVariant(void);
void pdguiSetInRoom(s32 inRoom);  /* pdgui_lobby.cpp — transition back to social lobby */
s32 pdguiCountdownIsActive(void); /* pdgui_bridge.c — true during pre-match countdown */
extern s32 g_NetMode;
extern s32 g_NetDedicated;
extern u8 g_NetMatchRoomId;
extern u8 g_NetCoopDifficulty;
extern u8 g_NetCoopFriendlyFire;
extern u8 g_NetCoopAiBuddies;
s32 getMaxAiBuddiesForDifficulty(s32 difficulty); /* mainmenu.c */
s32 pdguiRoomStartOfflineCoop(const char *stage_id, u8 difficulty); /* pdgui_bridge.c */
s32 bootConsumeDebugRoomTab(void); /* main.c: --debug-room-tab, once */

/* Connect code (for displaying to server host) */
s32 connectCodeEncode(u32 ip, char *buf, s32 bufsize);
const char *netGetPublicIP(void);

/* Lobby state */
void lobbyUpdate(void);
s32 lobbyGetPlayerCount(void);
s32 lobbyRoomGetPlayerCount(void);
s32 lobbyIsLocalLeader(void);


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
const char *lobbyRoomGetPlayerBodyId(s32 idx);
const char *lobbyRoomGetPlayerHeadId(s32 idx);

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
#define MPOPTION_SLOWMOTION_ON         0x00000040
#define MPOPTION_FASTMOVEMENT          0x00000100
#define MPOPTION_DISPLAYTEAM           0x00000200
#define MPOPTION_KILLSSCORE            0x00000400
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
/* Selection-time owner for the chosen scenario (matchsetup.c): syncs the
 * runtime mode with scenario_id and turns Teams on for team-only modes. */
s32 matchConfigApplyScenarioRules(const char *reason);
s32 matchConfigMaxBotsForHumans(s32 humanCount);
/* M0.1c: weapon slot catalog ID accessor (matchsetup.c) */
const char *matchGetWeaponSlotCatalogId(s32 slot);
s32 matchConfigSelectWeaponSet(s32 weaponsetnum);
s32 matchConfigSetWeaponSlotId(s32 slot, const char *weapon_id);

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

/* An arena's name for a list. A mod level carries its own name in the
 * catalog entry (arena.ini `display_name`, written by the mod author; Forge
 * F1): it has no string in the game's language files, and its template's
 * language id would show it as a second "Complex". Every other arena is
 * named by its language id, through arenaGetName above.
 * Non-static: the match setup list and both Grid lists use it too. */
const char *arenaEntryName(const asset_entry_t *e)
{
    if (!e) {
        return "???";
    }
    if (e->ext.arena.display_name[0]) {
        return e->ext.arena.display_name;
    }
    return arenaGetName((u16)e->ext.arena.name_langid);
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

    const char *name = arenaEntryName(e);
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
static u32 s_SpawnWeaponCatalogGeneration;

static void buildSpawnWeaponList(void)
{
    s_SpawnWeaponCatalogGeneration = assetCatalogGetGeneration();
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

/* Combat Sim custom weapon slots.
 *
 * This picker is intentionally catalog-ID native. Mod-authored weapons do not
 * have authored MPWEAPON_* IDs, so the menu cannot use mpGetWeaponLabel() /
 * mpSetWeaponSlot(), which are legacy numeric helpers.
 */
struct weaponoption_entry {
    char catalog_id[64];
    char name[64];
    s32  mp_weapon_id;
};

#define MAX_COMBAT_WEAPON_OPTIONS 128
static weaponoption_entry s_CombatWeaponOptions[MAX_COMBAT_WEAPON_OPTIONS];
static int s_NumCombatWeaponOptions = 0;

static void combatWeaponOptionCollect(const asset_entry_t *e, void *userdata)
{
    (void)userdata;
    if (!e || e->type != ASSET_WEAPON) return;
    if (s_NumCombatWeaponOptions >= MAX_COMBAT_WEAPON_OPTIONS) return;

    weaponoption_entry *w = &s_CombatWeaponOptions[s_NumCombatWeaponOptions++];
    strncpy(w->catalog_id, e->id, sizeof(w->catalog_id) - 1);
    w->catalog_id[sizeof(w->catalog_id) - 1] = '\0';
    const char *name = e->ext.weapon.name[0] ? e->ext.weapon.name : e->id;
    strncpy(w->name, name, sizeof(w->name) - 1);
    w->name[sizeof(w->name) - 1] = '\0';
    w->mp_weapon_id = e->ext.weapon.weapon_id;
}

static void buildCombatWeaponOptionList(void)
{
    s_NumCombatWeaponOptions = 0;
    assetCatalogIterateUnlockedByType(ASSET_WEAPON,
                                      combatWeaponOptionCollect, NULL);
    if (s_NumCombatWeaponOptions > 1) {
        std::sort(s_CombatWeaponOptions,
                  s_CombatWeaponOptions + s_NumCombatWeaponOptions,
                  [](const weaponoption_entry &a,
                     const weaponoption_entry &b) {
                      int cmp = strcasecmp(a.name, b.name);
                      if (cmp != 0) return cmp < 0;
                      return strcasecmp(a.catalog_id, b.catalog_id) < 0;
                  });
    }
}

static const weaponoption_entry *findCombatWeaponOptionById(const char *id)
{
    if (!id || !id[0]) return NULL;
    for (int i = 0; i < s_NumCombatWeaponOptions; i++) {
        if (strcmp(s_CombatWeaponOptions[i].catalog_id, id) == 0) {
            return &s_CombatWeaponOptions[i];
        }
    }
    return NULL;
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

enum RoomFocusRegion {
    ROOM_FOCUS_NONE = 0,
    ROOM_FOCUS_SETTINGS,
    ROOM_FOCUS_PLAYERS,
    ROOM_FOCUS_FOOTER,
};

enum RoomCsSection {
    ROOM_CS_PRESET = 0, /* Load Preset and Quick Start (Mike 2026-11-21) */
    ROOM_CS_ARENA,
    ROOM_CS_SCENARIO,
    ROOM_CS_LIMITS,
    ROOM_CS_WEAPONS,
    ROOM_CS_OPTIONS,
    ROOM_CS_SECTION_COUNT,
};

static int s_RoomFocusRegion = ROOM_FOCUS_NONE;
static int s_RoomCsFocusedSection = ROOM_CS_ARENA;
static int s_RoomCsSectionJumpPending = 0;
static int s_RoomCsFocusTarget = -1;

/* Rule 8 (2026-05-03): pending team-jump in the player panel.
 *
 * Set by the LT/RT poll at the top of pdguiRoomScreenRender when the
 * player panel owns focus; consumed by renderPlayerPanel which sets
 * keyboard focus on the first row of the target team. Direction:
 *   -1 = previous team's first player
 *   +1 = next team's first player
 *    0 = idle (no pending jump)
 *
 * The renderer clears the flag after consuming so the jump is one-shot. */
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
    s32  source;         /* 1 = public portrait, 2 = 3D bake, -1 = failed */
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

/* Public bot-profile picker. Unlike the old type/difficulty-only controls,
 * this retains custom .pdbotprofile catalog identity through match setup. */
#define MAX_ROOM_BOT_PROFILES 256
struct RoomBotProfileEntry {
    char id[CATALOG_ID_LEN];
    char name[96];
    u8 type;
    u8 difficulty;
};
static RoomBotProfileEntry s_RoomBotProfiles[MAX_ROOM_BOT_PROFILES];
static s32 s_RoomBotProfileCount = 0;

static void roomBotProfileCollect(const asset_entry_t *entry, void *userdata)
{
    (void)userdata;
    if (!entry || s_RoomBotProfileCount >= MAX_ROOM_BOT_PROFILES) return;
    RoomBotProfileEntry *out = &s_RoomBotProfiles[s_RoomBotProfileCount++];
    strncpy(out->id, entry->id, sizeof(out->id) - 1);
    out->id[sizeof(out->id) - 1] = '\0';
    out->type = entry->ext.bot_profile.type;
    out->difficulty = entry->ext.bot_profile.difficulty;
    if (entry->ext.bot_profile.name_langid) {
        snprintf(out->name, sizeof(out->name), "%s",
                 langGet(entry->ext.bot_profile.name_langid));
    } else if (out->type == 0 && out->difficulty < s_NumSimDiffs) {
        snprintf(out->name, sizeof(out->name), "%s", s_SimDiffNames[out->difficulty]);
    } else if (out->type < s_NumBotTypes) {
        snprintf(out->name, sizeof(out->name), "%s", s_BotTypeNames[out->type]);
    } else {
        snprintf(out->name, sizeof(out->name), "%s", entry->id);
    }
}

static void rebuildRoomBotProfileCache(void)
{
    s_RoomBotProfileCount = 0;
    assetCatalogIterateUnlockedByType(ASSET_BOT_PROFILE,
                                      roomBotProfileCollect, nullptr);
    std::sort(s_RoomBotProfiles,
              s_RoomBotProfiles + s_RoomBotProfileCount,
              [](const RoomBotProfileEntry &a, const RoomBotProfileEntry &b) {
                  const bool ag = a.type == 0;
                  const bool bg = b.type == 0;
                  if (ag != bg) return ag;
                  return strcasecmp(a.name, b.name) < 0;
              });
}

static const char *roomBotProfileName(const char *profile_id)
{
    if (!profile_id || !profile_id[0]) return "Unavailable";
    for (s32 i = 0; i < s_RoomBotProfileCount; i++) {
        if (strcmp(s_RoomBotProfiles[i].id, profile_id) == 0) {
            return s_RoomBotProfiles[i].name;
        }
    }
    return profile_id;
}

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

/* Load Preset and Quick Start (Mike 2026-11-17: presets are a Load Preset list
 * at the top of the Combat Simulator tab, host only, and picking one fills
 * every setting below, including the simulants; 2026-11-21: Presets and Quick
 * Start get a working entry, Challenges stay without a Room entry). They are the
 * original Combat Simulator menu's "Load/Preset Games" and "Quick Start". */
static int  s_PresetSelected    = -1;
static char s_PresetStatusMsg[128] = "";
static bool s_ShowQuickStart    = false;
static int  s_QuickStartMode    = 1;  /* see s_QuickStartModeNames */
static int  s_QuickStartSims    = 3;
static int  s_QuickStartDiff    = 2;  /* NormalSim */
/* >0: frames until Quick Start fires the Room's own Start Match. One frame lets
 * a networked room's settings go out (end-of-frame flush) before the start. */
static int  s_QuickStartFireFrames = 0;

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
 * Load Preset and Quick Start (Mike 2026-11-21)
 * ======================================================================== */

/* The Room's Load Preset: the original's preset game in that slot (counting the
 * unlocked ones) through scenarioLoadPreset, the same plan and commit as a saved
 * setup, so a refused preset leaves the match as it was. */
static void roomLoadPreset(int slot)
{
    char name[64];
    s32 humanCount = lobbyRoomGetPlayerCount();

    mpCopyPresetNameBySlot(slot, name, (s32)sizeof(name));
    if (humanCount < 1) humanCount = 1;
    if (!name[0]) snprintf(name, sizeof(name), "?");
    if (scenarioLoadPreset(slot, humanCount) == 0) {
        s_PresetSelected = slot;
        syncArenaFromConfig();
        syncSpawnWeaponFromConfig();
        s_RoomSettingsDirty = true;
        snprintf(s_PresetStatusMsg, sizeof(s_PresetStatusMsg), "Loaded preset: %s", name);
        sysLogPrintf(LOG_NOTE, "ROOM: preset loaded slot=%d name='%s' humans=%d",
                     slot, name, (int)humanCount);
        pdguiPlaySound(PDGUI_SND_SELECT);
    } else {
        snprintf(s_PresetStatusMsg, sizeof(s_PresetStatusMsg), "Could not load preset: %s", name);
        sysLogPrintf(LOG_WARNING, "ROOM: preset load failed slot=%d name='%s' humans=%d",
                     slot, name, (int)humanCount);
        pdguiPlaySound(PDGUI_SND_KBCANCEL);
    }
}

/* The original's Quick Team choices (g_MpQuickTeamMenuItems). */
#define ROOM_QS_PLAYERS_ONLY     0
#define ROOM_QS_PLAYERS_AND_SIMS 1
#define ROOM_QS_PLAYER_TEAMS     2
#define ROOM_QS_PLAYERS_VS_SIMS  3
#define ROOM_QS_PLAYER_SIM_TEAMS 4
#define ROOM_QS_MODE_COUNT       5

static const char *s_QuickStartModeNames[ROOM_QS_MODE_COUNT] = {
    "Players Only", "Players and Simulants", "Player Teams",
    "Players vs. Simulants", "Player-Simulant Teams",
};

static bool roomQuickStartModeHasSims(int mode)
{
    return mode == ROOM_QS_PLAYERS_AND_SIMS || mode == ROOM_QS_PLAYERS_VS_SIMS
        || mode == ROOM_QS_PLAYER_SIM_TEAMS;
}

/* Review of PR 27: Players vs. Simulants and Player-Simulant Teams set every
 * human's team, and a listen host cannot set a remote player's: each client
 * sends its own team (CLC_SETTINGS) and the match start takes it from there,
 * while a remote player has no match-config slot here. In a room with remote
 * players those two choices wait for decision D-056; offline they play as the
 * original. */
static bool roomQuickStartModeSetsHumanTeams(int mode)
{
    return mode == ROOM_QS_PLAYERS_VS_SIMS || mode == ROOM_QS_PLAYER_SIM_TEAMS;
}

static bool roomQuickStartRoomHasRemoteHumans(void)
{
    return netGetMode() == NETMODE_SERVER && lobbyRoomGetPlayerCount() > 1;
}

static bool roomQuickStartModeAvailable(int mode)
{
    return !(roomQuickStartModeSetsHumanTeams(mode) && roomQuickStartRoomHasRemoteHumans());
}

/* Quick Start: the original's Quick Team rules (mpConfigureQuickTeamPlayers and
 * mpConfigureQuickTeamSimulants, setup.c) applied to the Room's match. The
 * simulants are replaced; Teams goes off for Players Only and Players and
 * Simulants and on for the three team choices; Players vs. Simulants puts every
 * human on one team and the simulants on the other; Player-Simulant Teams gives
 * each human a team of its own with `sims` simulants on it; Player Teams keeps
 * the teams the humans picked in the player list. The mode's own rules apply
 * last, so King of the Hill and Capture the Case keep Teams on. Returns the
 * number of simulants added, or -1 when the choice needs simulants and none
 * could be added (a locked difficulty, or no room for one). */
static int roomQuickStartApply(int mode, int sims, int difficulty)
{
    const bool teams = mode == ROOM_QS_PLAYER_TEAMS || mode == ROOM_QS_PLAYERS_VS_SIMS
        || mode == ROOM_QS_PLAYER_SIM_TEAMS;
    int wanted = 0;
    int added = 0;
    int human = 0;

    for (int i = g_MatchConfig.numSlots - 1; i >= 1; i--) {
        if (g_MatchConfig.slots[i].type == SLOT_BOT) {
            matchConfigRemoveSlot(i);
        }
    }
    matchConfigSetUserOption(MPOPTION_TEAMSENABLED, teams ? 1 : 0);

    for (int i = 0; i < g_MatchConfig.numSlots; i++) {
        struct matchslot *slot = &g_MatchConfig.slots[i];
        if (slot->type != SLOT_PLAYER) continue;
        if (mode == ROOM_QS_PLAYERS_VS_SIMS) {
            slot->team = 0;
        } else if (mode == ROOM_QS_PLAYER_SIM_TEAMS) {
            slot->team = (u8)(human % 8);
        }
        human++;
    }

    if (mode == ROOM_QS_PLAYERS_AND_SIMS || mode == ROOM_QS_PLAYERS_VS_SIMS) {
        for (int b = 0; b < sims; b++) {
            s32 idx;
            wanted++;
            idx = matchConfigAddBot(0 /*BOTTYPE_GENERAL*/, (u8)difficulty, nullptr, nullptr, nullptr);
            if (idx < 0) break;
            g_MatchConfig.slots[idx].team = mode == ROOM_QS_PLAYERS_VS_SIMS ? 1 : 0;
            added++;
        }
    } else if (mode == ROOM_QS_PLAYER_SIM_TEAMS) {
        const int numHumanSlots = g_MatchConfig.numSlots;
        for (int i = 0; i < numHumanSlots; i++) {
            if (g_MatchConfig.slots[i].type != SLOT_PLAYER) continue;
            const u8 team = g_MatchConfig.slots[i].team;
            for (int b = 0; b < sims; b++) {
                s32 idx;
                wanted++;
                idx = matchConfigAddBot(0 /*BOTTYPE_GENERAL*/, (u8)difficulty, nullptr, nullptr, nullptr);
                if (idx < 0) break;
                g_MatchConfig.slots[idx].team = team;
                added++;
            }
        }
    }

    matchConfigApplyScenarioRules("quick-start");
    s_PresetSelected = -1;
    s_RoomSettingsDirty = true;
    /* Online the room's remote players have no slot here: count them too. */
    const int roomHumans = roomQuickStartRoomHasRemoteHumans()
        ? (int)lobbyRoomGetPlayerCount() : human;
    sysLogPrintf(LOG_NOTE,
                 "ROOM: Quick Start mode='%s' humans=%d sims=%d of %d difficulty=%d teams=%d",
                 s_QuickStartModeNames[mode], roomHumans, added, wanted, difficulty, teams ? 1 : 0);
    return (wanted > 0 && added == 0) ? -1 : added;
}

/* The most simulants the Quick Start slider offers for this many humans: the
 * Room's cap, divided among the humans for Player-Simulant Teams. */
static int roomQuickStartMaxSims(int mode)
{
    s32 humans = lobbyRoomGetPlayerCount();
    int max;

    if (humans < 1) humans = 1;
    max = matchConfigMaxBotsForHumans(humans);
    if (mode == ROOM_QS_PLAYER_SIM_TEAMS) max /= humans;
    return max < 1 ? 1 : max;
}

/* The Quick Start popup's Start: apply the choice, then the Room's own Start
 * Match fires two frames later (the footer counts s_QuickStartFireFrames down). */
static bool roomQuickStartBegin(int mode, int sims, int difficulty)
{
    if (!roomQuickStartModeAvailable(mode)) {
        snprintf(s_PresetStatusMsg, sizeof(s_PresetStatusMsg),
                 "%s sets every player's team: offline only for now.",
                 s_QuickStartModeNames[mode]);
        sysLogPrintf(LOG_WARNING,
                     "ROOM: Quick Start mode='%s' refused: the room has remote players, whose teams the host cannot set",
                     s_QuickStartModeNames[mode]);
        pdguiPlaySound(PDGUI_SND_KBCANCEL);
        return false;
    }
    if (roomQuickStartApply(mode, roomQuickStartModeHasSims(mode) ? sims : 0, difficulty) >= 0) {
        snprintf(s_PresetStatusMsg, sizeof(s_PresetStatusMsg), "Quick Start: %s",
                 s_QuickStartModeNames[mode]);
        s_QuickStartFireFrames = 2;
        pdguiPlaySound(PDGUI_SND_SELECT);
        return true;
    }
    snprintf(s_PresetStatusMsg, sizeof(s_PresetStatusMsg),
             "Quick Start could not add simulants at that difficulty.");
    pdguiPlaySound(PDGUI_SND_KBCANCEL);
    return false;
}

/* Smoke hooks for the Room's Load Preset and Quick Start (Mike 2026-11-21: they
 * get a working entry; the finalization batch proves them in the game). Read
 * once, they run once on the first leader frame of the Combat Simulator tab,
 * through the same calls as the preset list and the Quick Start popup's Start,
 * so a smoke started in the background needs no window focus to press them:
 *   --debug-room-load-preset <slot>  the slot-th unlocked preset (roomLoadPreset);
 *       with --debug-auto-start-match the match starts a frame later, with the
 *       preset's settings.
 *   --debug-room-quick-start <0..4>  Quick Start with that Quick Team choice
 *       (0 Players Only, 1 Players and Simulants, 2 Player Teams, 3 Players vs.
 *       Simulants, 4 Player-Simulant Teams), --debug-room-quick-start-sims <n>
 *       simulants (default 3, per human for 4) and
 *       --debug-room-quick-start-difficulty <0..5> (default 2, NormalSim). It
 *       fires Start Match itself, so it is not paired with
 *       --debug-auto-start-match. */
static int s_DebugSetupState = 0; /* 0 not read, 1 armed, 2 nothing left to run */
static int s_DebugPresetSlot = -1;
static int s_DebugQuickMode = -1;
static int s_DebugQuickSims = 3;
static int s_DebugQuickDiff = 2;

static void roomDebugSetupRead(void)
{
    if (s_DebugSetupState != 0) return;
    s_DebugSetupState = 2;
    if (sysArgCheck("--debug-room-load-preset")) {
        const s32 slot = sysArgGetInt("--debug-room-load-preset", -1);

        if (slot >= 0) {
            s_DebugPresetSlot = (int)slot;
            s_DebugSetupState = 1;
            sysLogPrintf(LOG_NOTE, "ROOM: --debug-room-load-preset armed slot=%d", (int)slot);
        } else {
            sysLogPrintf(LOG_WARNING, "ROOM: --debug-room-load-preset expects a slot 0 or more; ignored");
        }
    }
    if (sysArgCheck("--debug-room-quick-start")) {
        const s32 mode = sysArgGetInt("--debug-room-quick-start", -1);
        const s32 sims = sysArgGetInt("--debug-room-quick-start-sims", 3);
        const s32 diff = sysArgGetInt("--debug-room-quick-start-difficulty", 2);

        if (mode >= 0 && mode < ROOM_QS_MODE_COUNT && sims >= 1
                && diff >= 0 && diff < s_NumSimDiffs) {
            s_DebugQuickMode = (int)mode;
            s_DebugQuickSims = (int)sims;
            s_DebugQuickDiff = (int)diff;
            s_DebugSetupState = 1;
            sysLogPrintf(LOG_NOTE,
                         "ROOM: --debug-room-quick-start armed mode=%d sims=%d difficulty=%d",
                         (int)mode, (int)sims, (int)diff);
        } else {
            sysLogPrintf(LOG_WARNING,
                         "ROOM: --debug-room-quick-start expects a mode 0..%d, sims 1 or more and a difficulty 0..%d; ignored",
                         ROOM_QS_MODE_COUNT - 1, s_NumSimDiffs - 1);
        }
    }
}

/* True while a hook is armed and has not run, so the debug Start Match waits. */
static bool roomDebugSetupPending(void)
{
    roomDebugSetupRead();
    return s_DebugSetupState == 1;
}

/* Runs the armed hooks, once; the footer calls it after its Start Match. */
static void roomDebugSetupRun(void)
{
    if (!roomDebugSetupPending()) return;
    s_DebugSetupState = 2;
    if (s_DebugPresetSlot >= 0) {
        sysLogPrintf(LOG_NOTE, "ROOM: --debug-room-load-preset slot=%d of %d unlocked",
                     s_DebugPresetSlot, (int)mpGetNumUnlockedPresets());
        roomLoadPreset(s_DebugPresetSlot);
    }
    if (s_DebugQuickMode >= 0) {
        const int maxSims = roomQuickStartMaxSims(s_DebugQuickMode);
        const int sims = s_DebugQuickSims > maxSims ? maxSims : s_DebugQuickSims;
        const bool began = roomQuickStartBegin(s_DebugQuickMode, sims, s_DebugQuickDiff);

        sysLogPrintf(began ? LOG_NOTE : LOG_WARNING,
                     "ROOM: --debug-room-quick-start mode=%d sims=%d difficulty=%d %s",
                     s_DebugQuickMode, sims, s_DebugQuickDiff,
                     began ? "began" : "refused");
    }
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
 * (uses the first bot's difficulty; 0 is the canonical no-bot wire sentinel)
 * ======================================================================== */

static u8 getLeadSimType(void)
{
    for (int i = 1; i < g_MatchConfig.numSlots; i++) {
        if (g_MatchConfig.slots[i].type == SLOT_BOT) {
            return g_MatchConfig.slots[i].botDifficulty;
        }
    }
    return 0; /* No lead bot exists. */
}

static void roomRememberFocusedRegion(int region)
{
    s_RoomFocusRegion = region;
}

static void roomRememberFocusedPlayerRow(void)
{
    roomRememberFocusedRegion(ROOM_FOCUS_PLAYERS);
    if (!pdguiFriendsAnySurfaceIsOpen() && pdguiMenuStartPressed()) {
        s_StartMatchFocusPending = true;
        pdguiPlaySound(PDGUI_SND_SWIPE);
    }
}

static void roomCsSectionPrepareFocus(int section)
{
    if (s_RoomCsFocusTarget == section) {
        ImGui::SetKeyboardFocusHere(0);
        s_RoomCsFocusTarget = -1;
    }
}

static void roomCsSectionTrackLastItem(int section)
{
    if (ImGui::IsItemFocused()) {
        roomRememberFocusedRegion(ROOM_FOCUS_SETTINGS);
        s_RoomCsFocusedSection = section;
    }
}

static void roomCsHandlePendingSectionJump(void)
{
    if (s_RoomCsSectionJumpPending == 0) {
        return;
    }

    int target = s_RoomCsFocusedSection + s_RoomCsSectionJumpPending;
    if (target < 0) target = 0;
    if (target >= ROOM_CS_SECTION_COUNT) target = ROOM_CS_SECTION_COUNT - 1;

    if (target != s_RoomCsFocusedSection) {
        s_RoomCsFocusedSection = target;
        s_RoomCsFocusTarget = target;
    }

    s_RoomCsSectionJumpPending = 0;
}

static bool roomContextPopupRequestedForLastItem(void)
{
    if (pdguiFriendsAnySurfaceIsOpen()) return false;
    bool requested = false;
    if (ImGui::IsItemFocused() && pdguiMenuSecondaryPressed()) {
        requested = true;
        pdguiPlaySound(PDGUI_SND_SUBFOCUS);
    }
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
        requested = true;
    }
    return requested;
}

/* ========================================================================
 * Helper: option toggle row — renders a toggle button and updates bitmask
 * ======================================================================== */

static void optToggle(const char *label, u32 flag, bool leader)
{
    bool on = (matchConfigGetUserOptions() & flag) != 0;
    if (!leader) ImGui::BeginDisabled();
    /* Priority L (2026-04-25): pdguiCheckbox places the label on the LEFT
     * and plays its own TOGGLE sound; we replace the SUBFOCUS sound the
     * legacy optToggle used. */
    if (pdguiCheckbox(label, &on)) {
        matchConfigSetUserOption(flag, on ? 1 : 0);
        s_RoomSettingsDirty = true;
    }
    roomCsSectionTrackLastItem(ROOM_CS_OPTIONS);
    if (!leader) ImGui::EndDisabled();
}

/* Like optToggle but logic is inverted (flag ON means feature OFF) */
static void optToggleInverted(const char *label, u32 flag, bool leader)
{
    bool on = (matchConfigGetUserOptions() & flag) == 0; /* true when feature is enabled */
    if (!leader) ImGui::BeginDisabled();
    /* Priority L (2026-04-25): pdguiCheckbox places the label on the LEFT. */
    if (pdguiCheckbox(label, &on)) {
        matchConfigSetUserOption(flag, on ? 0 : 1);
        s_RoomSettingsDirty = true;
    }
    roomCsSectionTrackLastItem(ROOM_CS_OPTIONS);
    if (!leader) ImGui::EndDisabled();
}

/* ========================================================================
 * Per-player portrait helpers (S352 — D5 Phase 5)
 * ======================================================================== */

static void lobbyPortraitsReset(void)
{
    for (s32 i = 0; i < LOBBY_PORTRAIT_MAX; i++) {
        if (s_LobbyPortraits[i].glTex && s_LobbyPortraits[i].source == 2)
            pdguiCharPreviewFreeTexture(s_LobbyPortraits[i].glTex);
    }
    pdguiCharacterPortraitReset();
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
        if (s_LobbyPortraits[i].glTex && s_LobbyPortraits[i].source == 2) {
            pdguiCharPreviewFreeTexture(s_LobbyPortraits[i].glTex);
        }
        s_LobbyPortraits[i].glTex = 0;
        s_LobbyPortraits[i].source = 0;
        s_LobbyPortraits[i].head_id[0] = '\0';
        s_LobbyPortraits[i].body_id[0] = '\0';
        if (s_LobbyPortraitPending == i) {
            s_LobbyPortraitPending   = -1;
            s_LobbyPortraitWaitReady = false;
        }
    }

    for (s32 i = 0; i < humanCount && i < LOBBY_PORTRAIT_MAX; i++) {
        const char *hid = lobbyRoomGetPlayerHeadId(i);
        const char *bid = lobbyRoomGetPlayerBodyId(i);
        LobbyPortrait &p = s_LobbyPortraits[i];

        bool match = (hid && bid && hid[0] && bid[0] &&
                      strcmp(p.head_id, hid) == 0 &&
                      strcmp(p.body_id, bid) == 0);
        if (!match && p.glTex) {
            if (p.source == 2) pdguiCharPreviewFreeTexture(p.glTex);
            p.glTex = 0;
            if (s_LobbyPortraitPending == i) {
                s_LobbyPortraitPending   = -1;
                s_LobbyPortraitWaitReady = false;
            }
        }
        if (!match) {
            p.source = 0;
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
            if (tex && idx < LOBBY_PORTRAIT_MAX)
                s_LobbyPortraits[idx].source = 2;
            s_LobbyPortraitPending   = -1;
            s_LobbyPortraitWaitReady = false;
        }
        return;
    }

    for (s32 i = 0; i < humanCount && i < LOBBY_PORTRAIT_MAX; i++) {
        LobbyPortrait &p = s_LobbyPortraits[i];
        if (p.glTex || p.source != 0) continue;
        const char *hid = lobbyRoomGetPlayerHeadId(i);
        const char *bid = lobbyRoomGetPlayerBodyId(i);
        if (!hid || !hid[0] || !bid || !bid[0]) continue;

        u32 portraitTex = 0;
        s32 portraitResult = pdguiCharacterPortraitGet(
            bid, hid, &portraitTex, NULL, NULL);

        strncpy(p.head_id, hid, sizeof(p.head_id) - 1);
        p.head_id[sizeof(p.head_id) - 1] = '\0';
        strncpy(p.body_id, bid, sizeof(p.body_id) - 1);
        p.body_id[sizeof(p.body_id) - 1] = '\0';
        if (portraitResult > 0) {
            p.glTex = portraitTex;
            p.source = 1;
            continue;
        }
        if (portraitResult < 0) {
            p.source = -1;
            continue;
        }

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
    bool openChangeCharacterRequested = false;

    /* In solo mode there's no network lobby — local player count is always 1. */
    int humanCount = s_IsSoloMode ? 1 : lobbyRoomGetPlayerCount();
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
    bool teamsOn = (matchConfigGetUserOptions() & MPOPTION_TEAMSENABLED) != 0;

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
                               "  %d selected - Ctrl/Shift-click to multi-select, X for menu",
                               s_BotSelectCount);
        } else {
            ImGui::TextDisabled(
                "  Ctrl/Shift+Click on a bot row to multi-select; X for menu");
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
        if (ImGui::IsItemFocused()) {
            roomRememberFocusedPlayerRow();
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
        s32   state;       /* CLSTATE_* for humans, -1 for bots */
        u8    reviewing;   /* B-1190: still on the last match's results */
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
            if (!lobbyRoomGetPlayerInfo(i, &pv)) continue;
            RoomRow &r = rows[rowCount++];
            memset(&r, 0, sizeof(r));
            r.isBot = false;
            r.slotIdx = -1;
            r.lobbyIdx = i;
            r.team = pv.team;
            r.isLeader = pv.isLeader;
            r.isLocal = pv.isLocal ? 1 : 0;
            r.state = pv.state;
            r.reviewing = pv.reviewing ? 1 : 0;
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

    /* Rule 8 (2026-05-03): consume pending skip-up / skip-down for the
     * player panel. Per Mike's Q-A naming (SkipUp/SkipDown, was
     * SectionPrev/Next), Q-B (dynamic walker), Q-C (page-jump fallback
     * within panel; never crosses panels):
     *
     *   teamsOn  -> walk team boundaries, jump to next/prev team's first
     *               player. Boundary case: stays on first/last team
     *               (no wrap).
     *   teamsOff -> page-jump fallback: jump by visible-row-count rows
     *               within the panel scroll. Boundary clamps to first/
     *               last row.
     *
     * The cached focus row index (`s_FocusedRowCached`) is updated by
     * the row loop below via IsItemFocused so the next jump computes
     * relative to where the user actually is. */
    static s32 s_FocusedTeamCached = -1; /* updated by the row loop below */
    static s32 s_FocusedRowCached  = -1; /* updated by the row loop below */
    s32 sectionJumpTargetRowIdx = -1;
    if (rowCount > 0 && s_RoomPlayerSectionJumpPending != 0) {
        if (teamsOn) {
            /* Group walker: skip to next/prev team's first player. */
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
        } else {
            /* Q-C page-jump fallback: flat list, no groups. Jump by an
             * estimate of visible-row-count within the player panel
             * scroll. ImGui's GetWindowHeight gives the panel content
             * area; we have ~46 px / row (kHumanRowH 50 px or bot row
             * ~24 px; 32 px is a reasonable mid-row average for the
             * page-jump heuristic). The exact stride is not critical --
             * Mike's Q-C calls for "page-jump by visible-row-count"
             * which is naturally approximate. The boundary clamp at
             * first / last row matches the Rule 1 + Rule 8 no-wrap
             * contract. */
            const s32 kPageRows = 5;
            s32 base = (s_FocusedRowCached >= 0 && s_FocusedRowCached < rowCount)
                       ? s_FocusedRowCached : 0;
            s32 target = base + (kPageRows * s_RoomPlayerSectionJumpPending);
            if (target < 0) target = 0;
            if (target >= rowCount) target = rowCount - 1;
            if (target != base) {
                sectionJumpTargetRowIdx = target;
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

            /* Controller: secondary opens the context menu for the current
             * selection. Controller A activates the Selectable above via the
             * standard ImGui nav path and falls through to the same
             * single-select branch. Y is intentionally unbound on Room. */
            if (ImGui::IsItemFocused()) {
                roomRememberFocusedPlayerRow();
            }

            if (roomContextPopupRequestedForLastItem()) {
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

            /* Mike directive 2026-05-17: per-row context menu on the local
             * player row only (Rule 5 X-context-menu per
             * context/designs/input-menu/menu-input-interaction-grammar.md).
             * Right-click or controller secondary opens "##local_ctx".
             * Content emitted later in this loop iteration after the row
             * draws (mirrors the bot_ctx pattern). */
            if (ImGui::IsItemFocused()) {
                roomRememberFocusedPlayerRow();
            }

            if (r.isLocal && roomContextPopupRequestedForLastItem()) {
                ImGui::OpenPopup("##local_ctx");
            }

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
                             portrait->source == 1 ? ImVec2(0, 0) : ImVec2(0, 1),
                             portrait->source == 1 ? ImVec2(1, 1) : ImVec2(1, 0),
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
                const char *bodyId = lobbyRoomGetPlayerBodyId(r.lobbyIdx);
                const char *headId = lobbyRoomGetPlayerHeadId(r.lobbyIdx);
                const asset_entry_t *character =
                    assetCatalogFindCharacterByBodyHead(bodyId, headId);
                if (character && character->ext.character.display_name[0]) {
                    bodyName = character->ext.character.display_name;
                }
                if (!bodyName[0]) {
                    const asset_entry_t *body = assetCatalogResolve(bodyId);
                    bodyName = body && body->type == ASSET_BODY && body->ext.body.display_name[0]
                        ? body->ext.body.display_name : bodyId;
                }
                if (bodyName && bodyName[0]) {
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
                        if (r.reviewing) {
                            /* B-1190: still on the last match's results */
                            stateStr = "reviewing match stats";
                            stateCol = IM_COL32(120, 200, 255, (int)(alpha * 200));
                        } else {
                            stateStr = "ready";
                            stateCol = IM_COL32(80, 220, 80, (int)(alpha * 200));
                        }
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
                    ImGui::BeginTooltip();
                    if (hp->source == 1 && hp->glTex) {
                        float sz = pdguiScale(128.0f);
                        ImGui::Image((ImTextureID)(uintptr_t)hp->glTex,
                                     ImVec2(sz, sz));
                    } else if (hp->source == -1) {
                        ImGui::TextDisabled("Declared character portrait unavailable");
                    } else {
                        pdguiCharPreviewRequest(hp->head_id, hp->body_id);
                    }
                    if (hp->source == 2 && pdguiCharPreviewIsReady()) {
                        u32 liveTex = pdguiCharPreviewGetTextureId();
                        s32 pw = 0, ph = 0;
                        pdguiCharPreviewGetSize(&pw, &ph);
                        float scale = 128.0f / (float)(ph > 0 ? ph : 1);
                        ImGui::Image((ImTextureID)(uintptr_t)liveTex,
                                     ImVec2(pw * scale, ph * scale),
                                     ImVec2(0, 1), ImVec2(1, 0));
                    } else if (hp->source == 2 && hp->glTex) {
                        float sz = pdguiScale(128.0f);
                        ImGui::Image((ImTextureID)(uintptr_t)hp->glTex,
                                     ImVec2(sz, sz),
                                     ImVec2(0, 1), ImVec2(1, 0));
                    } else if (hp->source == 0) {
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

        /* Mike directive 2026-05-17: local-player context menu popup.
         * Mirrors the bot_ctx pattern below: emitted inline so PushID
         * scoping is per-row. Currently hosts "Change Character
         * (Temporary)" which previously lived as a standalone button
         * by Add Bot. The modal it opens is the same one the standalone
         * button opened. */
        if (r.isLocal && !r.isBot) {
            if (ImGui::BeginPopup("##local_ctx")) {
                ImGui::TextColored(pdguiVec4TitleGlow(), "%s", r.name);
                ImGui::Separator();

                const char *charItemLabel = s_RoomCharOverrideActive
                    ? "Change Character (Active, Temporary)"
                    : "Change Character (Temporary)";
                if (ImGui::MenuItem(charItemLabel)) {
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
                    openChangeCharacterRequested = true;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
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
            char commonProfile[CATALOG_ID_LEN] = {0};
            {
                bool first = true;
                for (int j = 1; j < g_MatchConfig.numSlots; j++) {
                    if (!s_BotSelected[j] || g_MatchConfig.slots[j].type != SLOT_BOT) continue;
                    int d = (int)g_MatchConfig.slots[j].botDifficulty;
                    int t = (int)g_MatchConfig.slots[j].botType;
                    int b = (int)g_MatchConfig.slots[j].bodynum;
                    if (first) {
                        commonDiff = d;
                        commonType = t;
                        commonBody = b;
                        strncpy(commonProfile,
                                g_MatchConfig.slots[j].profile_id,
                                sizeof(commonProfile) - 1);
                        first = false;
                    }
                    else {
                        if (commonDiff != d) commonDiff = -1;
                        if (commonType != t) commonType = -1;
                        if (commonBody != b) commonBody = -1;
                        if (strcmp(commonProfile,
                                   g_MatchConfig.slots[j].profile_id) != 0) {
                            commonProfile[0] = '\0';
                        }
                    }
                }
            }

            if (isLeader && ImGui::BeginMenu("Set Profile")) {
                rebuildRoomBotProfileCache();
                for (s32 pi = 0; pi < s_RoomBotProfileCount; pi++) {
                    const RoomBotProfileEntry &profile =
                        s_RoomBotProfiles[pi];
                    const bool selected = commonProfile[0]
                        && strcmp(commonProfile, profile.id) == 0;
                    char label[CATALOG_ID_LEN + 112];
                    snprintf(label, sizeof(label), "%s##profile_%s",
                             profile.name, profile.id);
                    if (ImGui::MenuItem(label, nullptr, selected)) {
                        for (int j = 1; j < g_MatchConfig.numSlots; j++) {
                            if (s_BotSelected[j]
                                && g_MatchConfig.slots[j].type == SLOT_BOT) {
                                matchConfigSetBotProfile(j, profile.id);
                            }
                        }
                        pdguiPlaySound(PDGUI_SND_SUBFOCUS);
                        s_RoomSettingsDirty = true;
                    }
                }
                ImGui::EndMenu();
            }

            /* Set AI Type — simulant difficulty preset. Applies to every
             * selected bot (MeatSim / EasySim / NormalSim / HardSim / ...). */
            if (isLeader && ImGui::BeginMenu("Set AI Type")) {
                for (int d = 0; d < s_NumSimDiffs; d++) {
                    if (ImGui::MenuItem(s_SimDiffNames[d], NULL, d == commonDiff)) {
                        for (int j = 1; j < g_MatchConfig.numSlots; j++) {
                            if (s_BotSelected[j]
                                && g_MatchConfig.slots[j].type == SLOT_BOT) {
                                matchConfigSetBotTraits(j,
                                    g_MatchConfig.slots[j].botType, (u8)d);
                            }
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
                            if (s_BotSelected[j]
                                && g_MatchConfig.slots[j].type == SLOT_BOT) {
                                matchConfigSetBotTraits(j, (u8)t,
                                    g_MatchConfig.slots[j].botDifficulty);
                            }
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
                        if (matchConfigAddBotWithProfile(src->profile_id,
                                              src->body_id, src->head_id,
                                              NULL) >= 0) {
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

            /* Random name — random name per bot, body/head untouched. Each
             * bot rolls independently so a multi-select doesn't end up with a
             * shared name. */
            if (isLeader && ImGui::MenuItem("Random name")) {
                for (int j = 1; j < g_MatchConfig.numSlots; j++) {
                    if (s_BotSelected[j] && g_MatchConfig.slots[j].type == SLOT_BOT) {
                        matchConfigRerollBotName(j);
                    }
                }
                pdguiPlaySound(PDGUI_SND_SUBFOCUS);
                s_RoomSettingsDirty = true;
            }

            /* Random name + character (existing full re-roll). */
            if (isLeader && ImGui::MenuItem("Random name + character")) {
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

        /* Rule 8 (2026-05-03): track focused team + row index for the
         * next LT/RT skip-up / skip-down. When ANY widget within this
         * row has focus (the Selectable, the popup, etc.), cache the
         * row's team and the row index so the next jump computes
         * relative to where the user actually is. teamsOn uses the team
         * cache for the group walker; teamsOff uses the row-index cache
         * for the page-jump fallback (Q-C). Falls back to team 0 / row
         * 0 when no row has had focus yet (e.g. fresh panel entry). */
        if (ImGui::IsItemFocused()) {
            roomRememberFocusedPlayerRow();
            s_FocusedTeamCached = (s32)r.team;
            s_FocusedRowCached  = ri;
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
    if (ImGui::IsItemFocused()) {
        roomRememberFocusedPlayerRow();
    }
    if (roomContextPopupRequestedForLastItem()) {
        ImGui::OpenPopup("##add_bot_ctx");
    }
    if (!canAdd) ImGui::EndDisabled();

    if (ImGui::BeginPopup("##add_bot_ctx")) {
        if (!isLeader) ImGui::BeginDisabled();
        if (ImGui::MenuItem("Add Bot", NULL, false, canAdd)) {
            matchConfigAddBot(0 /*BOTTYPE_NORMAL*/, 2 /*NormalSim*/, nullptr, nullptr, nullptr);
            pdguiPlaySound(PDGUI_SND_SUBFOCUS);
            s_RoomSettingsDirty = true;
        }
        if (ImGui::MenuItem("Fill Bot Slots", NULL, false, canAdd)) {
            while (countBots() < maxBots && g_MatchConfig.numSlots < MATCH_MAX_SLOTS) {
                matchConfigAddBot(0 /*BOTTYPE_NORMAL*/, 2 /*NormalSim*/, nullptr, nullptr, nullptr);
            }
            pdguiPlaySound(PDGUI_SND_SUBFOCUS);
            s_RoomSettingsDirty = true;
        }
        const bool hasBots = (countBots() > 0);
        if (ImGui::MenuItem("Remove All Bots", NULL, false, hasBots)) {
            for (int j = g_MatchConfig.numSlots - 1; j >= 1; j--) {
                if (g_MatchConfig.slots[j].type == SLOT_BOT) {
                    matchConfigRemoveSlot(j);
                }
            }
            botSelectClear();
            s_BotLastClickedSlot = -1;
            pdguiPlaySound(PDGUI_SND_KBCANCEL);
            s_RoomSettingsDirty = true;
        }
        if (!isLeader) ImGui::EndDisabled();
        ImGui::EndPopup();
    }

    /* Mike directive 2026-05-17: "Change Character (Temporary)" moved
     * from this standalone button to the per-row context menu on the
     * local player row only (see "##local_ctx" popup ~line 2350 in
     * the roster loop). Right-click the local row (or controller
     * secondary) to access. The modal it opens is unchanged -- only
     * the access path moved. */

    /* Modal: body + head pickers, Apply / Cancel / Reset-to-Saved.
     * BeginPopupModal floats above the parent regardless of where it's
     * rendered in the parent's tree. Pool registration not required;
     * popup tears down with the Room screen automatically. */
    if (openChangeCharacterRequested) {
        /* The roster row/context scopes have unwound. The outer player
         * panel now owns both the OpenPopup and BeginPopupModal ID. */
        ImGui::OpenPopup("##room_change_char_modal");
        s_ShowChangeCharModal = true;
        pdguiNavSuppressActivation();
    }
    if (s_ShowChangeCharModal) {
        ImGui::SetNextWindowSizeConstraints(
            ImVec2(pdguiScale(420.0f), pdguiScale(360.0f)),
            ImVec2(pdguiScale(640.0f), pdguiScale(640.0f)));
        if (ImGui::BeginPopupModal("##room_change_char_modal",
                                    NULL,
                                    ImGuiWindowFlags_NoSavedSettings)) {
            const bool cancelCharacter = pdguiMenuCancelPressed();
            pdguiPopupDarkenBehind(0.65f);
            ImGui::TextColored(pdguiVec4TitleGlow(),
                               "Change Character (this match only)");
            ImGui::TextDisabled(
                "Applies for this room session. Your saved Agent character is unchanged.");
            ImGui::Separator();

            /* ---- Body list ---- */
            struct CharEntry {
                char id[64];
                char head_id[64];
                const char *name;
                s32 mp_idx;
            };
            static CharEntry s_ModalCharacters[256];
            static s32 s_ModalCharacterCount = 0;
            static CharEntry s_ModalBodies[256];
            static s32 s_ModalBodyCount = 0;
            static CharEntry s_ModalHeads[256];
            static s32 s_ModalHeadCount = 0;

            /* Rebuild lists when popup first appears (catches catalog
             * unlock changes between opens). */
            if (ImGui::IsWindowAppearing()) {
                s_ModalCharacterCount = 0;
                struct CharacterCtx { CharEntry *out; s32 *count; };
                CharacterCtx cctx = {
                    s_ModalCharacters, &s_ModalCharacterCount
                };
                auto ccb = +[](const asset_entry_t *e, void *ud) {
                    CharacterCtx *c = (CharacterCtx *)ud;
                    if (*c->count >= 256) return;
                    const asset_runtime_binding_t *binding =
                        assetRuntimeFindByTypeAndId(ASSET_CHARACTER, e->id);
                    if (!binding || !binding->character_body_id[0] ||
                            !binding->character_head_id[0]) return;
                    CharEntry *ce = &c->out[(*c->count)++];
                    strncpy(ce->id, binding->character_body_id,
                            sizeof(ce->id) - 1);
                    ce->id[sizeof(ce->id) - 1] = '\0';
                    strncpy(ce->head_id, binding->character_head_id,
                            sizeof(ce->head_id) - 1);
                    ce->head_id[sizeof(ce->head_id) - 1] = '\0';
                    ce->name = binding->display_name[0]
                        ? binding->display_name : e->id;
                    ce->mp_idx = -1;
                };
                assetCatalogIterateUnlockedByType(
                    ASSET_CHARACTER, ccb, &cctx);

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

            ImGui::Text("Character");
            ImGui::BeginChild("##char_character_list",
                              ImVec2(pdguiScale(380.0f), pdguiScale(110.0f)),
                              true);
            for (s32 i = 0; i < s_ModalCharacterCount; i++) {
                bool sel = strcmp(s_ModalCharacters[i].id,
                                  s_PendingCharBodyId) == 0 &&
                           strcmp(s_ModalCharacters[i].head_id,
                                  s_PendingCharHeadId) == 0;
                if (ImGui::Selectable(s_ModalCharacters[i].name, sel)) {
                    strncpy(s_PendingCharBodyId, s_ModalCharacters[i].id,
                            sizeof(s_PendingCharBodyId) - 1);
                    s_PendingCharBodyId[sizeof(s_PendingCharBodyId) - 1] = '\0';
                    strncpy(s_PendingCharHeadId,
                            s_ModalCharacters[i].head_id,
                            sizeof(s_PendingCharHeadId) - 1);
                    s_PendingCharHeadId[sizeof(s_PendingCharHeadId) - 1] = '\0';
                }
                if (sel) ImGui::SetItemDefaultFocus();
            }
            if (s_ModalCharacterCount == 0) {
                ImGui::TextDisabled("No complete public character sources loaded.");
            }
            ImGui::EndChild();

            ImGui::TextDisabled("Advanced assembly");

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
                              ImVec2(actBtnW, actBtnH)) && !cancelCharacter) {
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
                              ImVec2(actBtnW, actBtnH)) || cancelCharacter) {
                s_ShowChangeCharModal = false;
                ImGui::CloseCurrentPopup();
            }
            if (s_RoomCharOverrideActive) {
                ImGui::SameLine();
                if (ImGui::Button("Reset to Saved###char_modal_reset",
                                  ImVec2(pdguiScale(150.0f), actBtnH)) && !cancelCharacter) {
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

    roomCsHandlePendingSectionJump();

    /* --- Preset --- (Mike 2026-11-17 / 2026-11-21: Load Preset at the top of
     * the Combat Simulator tab, host only, and Quick Start beside it; picking a
     * preset fills every setting below and the host can still change anything) */
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.9f, 1.0f), "Preset");
    if (!leader) ImGui::BeginDisabled();
    {
        const float quickW = pdguiScale(150.0f);
        const float presetW = comboW - quickW - ImGui::GetStyle().ItemSpacing.x;
        const int numPresets = (int)mpGetNumUnlockedPresets();
        char presetLabel[64];

        snprintf(presetLabel, sizeof(presetLabel), "Load Preset...");
        if (s_PresetSelected >= 0 && s_PresetSelected < numPresets) {
            char selectedName[64];
            mpCopyPresetNameBySlot(s_PresetSelected, selectedName, (s32)sizeof(selectedName));
            if (selectedName[0]) snprintf(presetLabel, sizeof(presetLabel), "%s", selectedName);
        }
        roomCsSectionPrepareFocus(ROOM_CS_PRESET);
        ImGui::SetNextItemWidth(presetW > 0.0f ? presetW : comboW);
        if (ImGui::BeginCombo("##preset", presetLabel)) {
            for (int p = 0; p < numPresets; p++) {
                char presetName[64];
                mpCopyPresetNameBySlot(p, presetName, (s32)sizeof(presetName));
                ImGui::PushID(p);
                if (ImGui::Selectable(presetName[0] ? presetName : "?",
                                      p == s_PresetSelected)) {
                    roomLoadPreset(p);
                }
                if (p == s_PresetSelected) ImGui::SetItemDefaultFocus();
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        roomCsSectionTrackLastItem(ROOM_CS_PRESET);
        ImGui::SameLine();
        if (ImGui::Button("Quick Start", ImVec2(quickW, 0.0f))) {
            int maxSims = roomQuickStartMaxSims(s_QuickStartMode);
            if (s_QuickStartSims > maxSims) s_QuickStartSims = maxSims;
            s_ShowQuickStart = true;
            pdguiPlaySound(PDGUI_SND_SUBFOCUS);
        }
        roomCsSectionTrackLastItem(ROOM_CS_PRESET);
    }
    if (!leader) ImGui::EndDisabled();
    if (s_PresetStatusMsg[0]) {
        ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), "%s", s_PresetStatusMsg);
    }

    ImGui::Spacing();

    /* --- Arena --- */
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.9f, 1.0f), "Arena");
    if (!leader) ImGui::BeginDisabled();
    roomCsSectionPrepareFocus(ROOM_CS_ARENA);
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
            ImGui::TextDisabled("%s", hdr);
            ImGui::Indent();
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
            ImGui::Unindent();
        }
        ImGui::EndCombo();
    }
    roomCsSectionTrackLastItem(ROOM_CS_ARENA);
    if (!leader) ImGui::EndDisabled();

    ImGui::Spacing();

    /* --- Scenario --- */
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.9f, 1.0f), "Scenario");
    if (!leader) ImGui::BeginDisabled();
    roomCsSectionPrepareFocus(ROOM_CS_SCENARIO);
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
                /* A team-only mode turns Teams on here, as picking it did
                 * in the original menus, so the option and the team pickers
                 * show it before Start. The dirty flag below broadcasts it. */
                matchConfigApplyScenarioRules("room-select");
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
    roomCsSectionTrackLastItem(ROOM_CS_SCENARIO);
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
        roomCsSectionPrepareFocus(ROOM_CS_LIMITS);
        ImGui::SetNextItemWidth(comboW * 0.6f);
        /* Priority L (2026-04-25): label LEFT via pdguiSliderInt. */
        if (pdguiSliderInt("Time (min)", &tl, 1, 61)) {
            g_MatchConfig.timelimit = (u8)(tl - 1);  /* store 0-based */
            s_RoomSettingsDirty = true;
        }
        roomCsSectionTrackLastItem(ROOM_CS_LIMITS);
        ImGui::SameLine();
        if (g_MatchConfig.timelimit >= 60) {
            ImGui::TextColored(ImVec4(0.5f, 0.7f, 0.5f, 1.0f), "No limit");
        } else {
            ImGui::Text("%d min", tl);
        }
        if (!leader) ImGui::EndDisabled();
    }

    /* Score limit: slider shows 1–100 kills directly; 101 = no limit.
     * Stored as 0-based (scorelimit = kills - 1); storage value 100 = no limit. */
    {
        if (!leader) ImGui::BeginDisabled();
        int sl = (int)g_MatchConfig.scorelimit + 1;  /* convert to 1-based for display */
        ImGui::SetNextItemWidth(comboW * 0.6f);
        /* Priority L (2026-04-25): label LEFT via pdguiSliderInt. */
        if (pdguiSliderInt("Score", &sl, 1, 101)) {
            g_MatchConfig.scorelimit = (u8)(sl - 1);  /* store 0-based */
            s_RoomSettingsDirty = true;
        }
        roomCsSectionTrackLastItem(ROOM_CS_LIMITS);
        ImGui::SameLine();
        if (g_MatchConfig.scorelimit >= 100) {
            ImGui::TextColored(ImVec4(0.5f, 0.7f, 0.5f, 1.0f), "No limit");
        } else {
            ImGui::Text("%d kills", sl);  /* sl already equals scorelimit+1 */
        }
        if (!leader) ImGui::EndDisabled();
    }

    /* Team score limit (T-NETWORKING-011, finding 9): the original Team Score
     * slider (menuhandlerMpTeamScoreLimitSlider) stores 0..400 with 400 =
     * no limit and shows the value + 1. The Room fixed it at 400. Shown with
     * teams on; same 1-based display and 0-based store as the sliders above. */
    if (matchConfigGetUserOptions() & MPOPTION_TEAMSENABLED) {
        if (!leader) ImGui::BeginDisabled();
        int tsl = (int)g_MatchConfig.teamscorelimit + 1;
        if (tsl > 401) tsl = 401;
        ImGui::SetNextItemWidth(comboW * 0.6f);
        if (pdguiSliderInt("Team Score", &tsl, 1, 401)) {
            g_MatchConfig.teamscorelimit = (u16)(tsl - 1);
            s_RoomSettingsDirty = true;
        }
        roomCsSectionTrackLastItem(ROOM_CS_LIMITS);
        ImGui::SameLine();
        if (g_MatchConfig.teamscorelimit >= 400) {
            ImGui::TextColored(ImVec4(0.5f, 0.7f, 0.5f, 1.0f), "No limit");
        } else {
            ImGui::Text("%d points", tsl);
        }
        if (!leader) ImGui::EndDisabled();
    }

    ImGui::Spacing();

    /* --- Weapon Set (alphabetized; group headers are non-focusable text) --- */
    {
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.9f, 1.0f), "Weapon Set");
        if (!leader) ImGui::BeginDisabled();
        roomCsSectionPrepareFocus(ROOM_CS_WEAPONS);
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
        ImGui::TextDisabled("%s", grpHdr);
        ImGui::Indent();
        for (s32 j = 0; j < wcount; j++) {
            bool isSel = (s_WSorted[j].idx == curSet);
            char setLabel[128];
            snprintf(setLabel, sizeof(setLabel), "%s##ws%d", s_WSorted[j].name, s_WSorted[j].idx);
            if (ImGui::Selectable(setLabel, isSel)) {
                if (matchConfigSelectWeaponSet(s_WSorted[j].idx)) {
                    pdguiPlaySound(PDGUI_SND_SUBFOCUS);
                    s_RoomSettingsDirty = true;
                }
            }
            roomCsSectionTrackLastItem(ROOM_CS_WEAPONS);
            if (isSel) ImGui::SetItemDefaultFocus();
        }
        ImGui::Unindent();
        if (!leader) ImGui::EndDisabled();
    }

    /* --- Custom Weapon Slots (visible when Custom set selected) --- */
    if (g_MpWeaponSetNum == WEAPONSET_CUSTOM) {
        ImGui::Spacing();
        buildCombatWeaponOptionList();
        static const char *slotLabels[NUM_MPWEAPONSLOTS] = {
            "Slot 1##cws", "Slot 2##cws", "Slot 3##cws",
            "Slot 4##cws", "Slot 5##cws", "Slot 6##cws"
        };

        if (!leader) ImGui::BeginDisabled();
        for (s32 slot = 0; slot < NUM_MPWEAPONSLOTS; slot++) {
            const char *curCatalogId = g_MatchConfig.weapon_ids[slot][0]
                ? g_MatchConfig.weapon_ids[slot]
                : matchGetWeaponSlotCatalogId(slot);
            const weaponoption_entry *curWeapon =
                findCombatWeaponOptionById(curCatalogId);
            const char *curWeaponName = curWeapon ? curWeapon->name
                : (curCatalogId && curCatalogId[0] ? curCatalogId : "None");
            ImGui::SetNextItemWidth(comboW);
            if (ImGui::BeginCombo(slotLabels[slot],
                                  curWeaponName ? curWeaponName : "???")) {
                for (s32 w = 0; w < s_NumCombatWeaponOptions; w++) {
                    const weaponoption_entry *opt = &s_CombatWeaponOptions[w];
                    bool isSel = (curCatalogId && curCatalogId[0]
                        && strcmp(curCatalogId, opt->catalog_id) == 0);
                    char wLabel[64];
                    snprintf(wLabel, sizeof(wLabel), "%s##cws%d_%d",
                             opt->name, slot, w);
                    if (ImGui::Selectable(wLabel, isSel)) {
                        if (matchConfigSetWeaponSlotId(slot, opt->catalog_id)) {
                            pdguiPlaySound(PDGUI_SND_SUBFOCUS);
                            s_RoomSettingsDirty = true;
                        }
                    }
                    if (isSel) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            roomCsSectionTrackLastItem(ROOM_CS_WEAPONS);
        }
        if (!leader) ImGui::EndDisabled();
    }

    ImGui::Spacing();
    ImGui::Separator();

    /* --- Game Options --- */
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.9f, 1.0f), "Options");

    roomCsSectionPrepareFocus(ROOM_CS_OPTIONS);
    optToggleInverted("Auto-Aim",        MPOPTION_NOAUTOAIM,         leader);
    optToggleInverted("Radar",           MPOPTION_NORADAR,           leader);
    optToggle        ("Teams",           MPOPTION_TEAMSENABLED,      leader);
    /* Finding 9: the original options menus offer Display Team in every
     * scenario (team colours on other players); it only means something with
     * teams on. */
    if (matchConfigGetUserOptions() & MPOPTION_TEAMSENABLED) {
        optToggle    ("Display Team",    MPOPTION_DISPLAYTEAM,       leader);
    }
    optToggle        ("One-Hit Kills",   MPOPTION_ONEHITKILLS,       leader);
    optToggle        ("Friendly Fire",   MPOPTION_FRIENDLYFIRE,      leader);
    optToggle        ("Fast Movement",   MPOPTION_FASTMOVEMENT,      leader);
    /* Mike directive 2026-05-17: bot jumping toggle (default OFF). Lives
     * in src/include/constants.h:2952 as 0x10000000; the Room screen
     * re-declares the option constants locally above (B-194 pattern). */
#ifndef MPOPTION_BOTJUMP
#define MPOPTION_BOTJUMP                 0x10000000
#endif
    optToggle        ("Bot Jumping",     MPOPTION_BOTJUMP,           leader);
    optToggle        ("Spawn w/ Weapon", MPOPTION_SPAWNWITHWEAPON,   leader);
    /* Weapon selector — only visible when spawn-with-weapon is on.
     *
     * S482 (2026-04-27): three-mode selector. Random = roll once at match
     * start (fixed for the match); Fiesta = roll fresh per spawn per player;
     * specific weapon = always that weapon. */
    if (matchConfigGetUserOptions() & MPOPTION_SPAWNWITHWEAPON) {
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
        roomCsSectionTrackLastItem(ROOM_CS_OPTIONS);
        if (!leader) ImGui::EndDisabled();
    }
    optToggleInverted("Player Highlight",MPOPTION_NOPLAYERHIGHLIGHT, leader);
    optToggleInverted("Pickup Highlight",MPOPTION_NOPICKUPHIGHLIGHT, leader);
    optToggle        ("No Doors",        MPOPTION_NODOORS,           leader);
    optToggle        ("Slow Motion",     MPOPTION_SLOWMOTION_ON,     leader);

    ImGui::Spacing();

    /* Priority L (2026-04-25, finish-menus pass): Handicaps now renders
     * INLINE inside Match Settings rather than pushing a separate modal
     * dialog. Per Mike's flat-menu rule 6,
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

        /* Player Handicaps -- inline rows. Header text is not focusable. */
        ImGui::TextDisabled("Player Handicaps");
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
            roomCsSectionTrackLastItem(ROOM_CS_OPTIONS);
            ImGui::PopID();
            playerSlot++;
        }
        if (!anyPlayerSlot) {
            ImGui::TextDisabled("No human player slots in this match.");
        }
        ImGui::Unindent();

        /* Team Setup keeps modal -- multi-team naming + per-slot
         * reassignment grid has its own focus model per the methodology
         * rule 6 exception. */
        if (ImGui::Button("Team Setup...", ImVec2(subBtnW, subBtnH))) {
            menuGraphFirePushDialog(MENU_TYPE_ROOM, "team_setup",
                                    &g_MpTeamsMenuDialog);
            pdguiPlaySound(PDGUI_SND_SELECT);
        }
        roomCsSectionTrackLastItem(ROOM_CS_OPTIONS);
        if (!leader) ImGui::EndDisabled();

        /* Music selection — available to all players (personal playlist choice) */
        if (ImGui::Button("Select Music...", ImVec2(subBtnW, subBtnH))) {
            menuGraphFirePushDialog(MENU_TYPE_ROOM, "select_music",
                                    &g_MpSelectTunesMenuDialog);
            pdguiPlaySound(PDGUI_SND_SELECT);
        }
        roomCsSectionTrackLastItem(ROOM_CS_OPTIONS);
    }

    /* Scenario-specific options. Finding 9: Kills Score (kills add to the
     * score) is in the original options of Hold the Briefcase, Hacker
     * Central, King of the Hill and Capture the Case, not Pop a Cap. */
    int sc = (int)g_MatchConfig.scenario;
    if (sc == MPSCENARIO_HOLDTHEBRIEFCASE) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.8f, 0.9f), "Hold the Briefcase");
        optToggle("Highlight Briefcase", MPOPTION_HTB_HIGHLIGHTBRIEFCASE, leader);
        optToggle("Show on Radar",       MPOPTION_HTB_SHOWONRADAR,        leader);
        optToggle("Kills Score",         MPOPTION_KILLSSCORE,             leader);
    } else if (sc == MPSCENARIO_CAPTURETHECASE) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.8f, 0.9f), "Capture the Case");
        optToggle("Show on Radar", MPOPTION_CTC_SHOWONRADAR, leader);
        optToggle("Kills Score",   MPOPTION_KILLSSCORE,      leader);
    } else if (sc == MPSCENARIO_KINGOFTHEHILL) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.8f, 0.9f), "King of the Hill");
        optToggle("Hill on Radar",  MPOPTION_KOH_HILLONRADAR, leader);
        optToggle("Mobile Hill",    MPOPTION_KOH_MOBILEHILL,  leader);
        optToggle("Kills Score",    MPOPTION_KILLSSCORE,      leader);
    } else if (sc == MPSCENARIO_HACKERCENTRAL) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.8f, 0.9f), "Hacker Central");
        optToggle("Highlight Terminal", MPOPTION_HTM_HIGHLIGHTTERMINAL, leader);
        optToggle("Show on Radar",      MPOPTION_HTM_SHOWONRADAR,       leader);
        optToggle("Kills Score",        MPOPTION_KILLSSCORE,            leader);
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
    roomCsSectionTrackLastItem(ROOM_CS_OPTIONS);
    ImGui::SameLine();
    if (ImGui::Button("Load Scenario", ImVec2(halfW, sbtnH))) {
        s_ScenarioCount = scenarioListFiles(s_ScenarioFiles, SCENARIO_MAX_LIST);
        s_ScenarioSelected = -1;
        s_ShowLoadScenario = true;
        s_ScenarioStatusMsg[0] = '\0';
        pdguiPlaySound(PDGUI_SND_SUBFOCUS);
    }
    roomCsSectionTrackLastItem(ROOM_CS_OPTIONS);
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

    /* --- Partner (co-op AI buddies, Mike 2026-11-16 and 2026-11-17) ---
     * Co-op is for two players. A second player in this Room is the
     * partner; a host who starts alone takes simulant buddies, as the
     * original's Perfect Buddy option offered with one controller. The
     * limit is the original's (getMaxAiBuddies): one buddy, more once the
     * co-op campaign is finished on harder difficulties. The stage start
     * clamps the choice again and uses it only when the host is alone. */
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.9f, 1.0f), "Partner");
    {
        const s32 players = s_IsSoloMode ? 1 : lobbyRoomGetPlayerCount();

        if (players == 2) {
            ImGui::TextDisabled("The other player in this Room is your partner.");
        } else if (players > 2) {
            ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.3f, 1.0f),
                "Co-op is for two players; this Room has %d.", (int)players);
        } else {
            static const char *const s_BuddyNames[] = {
                "1 Simulant", "2 Simulants", "3 Simulants", "4 Simulants"
            };
            const s32 maxBuddies = getMaxAiBuddiesForDifficulty(s_CampaignDiff);
            s32 shown = g_NetCoopAiBuddies;

            if (shown < 1) shown = 1;
            if (shown > maxBuddies) shown = maxBuddies;
            if (!leader) ImGui::BeginDisabled();
            ImGui::SetNextItemWidth(comboW);
            if (ImGui::BeginCombo("##aibuddies", s_BuddyNames[shown - 1])) {
                for (s32 bi = 1; bi <= maxBuddies; bi++) {
                    bool sel = (bi == shown);
                    if (ImGui::Selectable(s_BuddyNames[bi - 1], sel)) {
                        g_NetCoopAiBuddies = (u8)bi;
                        pdguiPlaySound(PDGUI_SND_SUBFOCUS);
                    }
                    if (sel) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            if (!leader) ImGui::EndDisabled();
            ImGui::TextDisabled("Simulant buddies play the mission with you.");
            ImGui::TextDisabled("%s", s_IsSoloMode
                ? "To play with a friend, host a Room instead."
                : "A friend who joins this Room is your partner instead.");
            if (maxBuddies < 4) {
                ImGui::TextDisabled("Finish the co-op campaign on harder difficulties for more.");
            }
        }
    }

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
    s32 playerCount = lobbyRoomGetPlayerCount();
    char antiPlayerName[sizeof(lobbyplayer_view::name)] = "Player 2";
    s32 selectedRow = -1;
    for (s32 pi = 0; pi < playerCount; ++pi) {
        struct lobbyplayer_view pv = {};
        if (lobbyRoomGetPlayerInfo(pi, &pv) && pv.clientId == s_CounterOpClientId) {
            selectedRow = pi;
            break;
        }
    }
    if (selectedRow < 0 && playerCount >= 2) {
        selectedRow = 0;
        for (s32 pi = 0; pi < playerCount; ++pi) {
            struct lobbyplayer_view pv = {};
            if (lobbyRoomGetPlayerInfo(pi, &pv) && !pv.isLocal) {
                selectedRow = pi;
                break;
            }
        }
    }
    s_CounterOpPlayer = selectedRow;
    s_CounterOpClientId = 0xff;
    struct lobbyplayer_view selectedPlayer = {};
    if (selectedRow >= 0 && lobbyRoomGetPlayerInfo(selectedRow, &selectedPlayer)) {
        s_CounterOpClientId = selectedPlayer.clientId;
        snprintf(antiPlayerName, sizeof(antiPlayerName), "%s", selectedPlayer.name);
    }

    ImGui::SetNextItemWidth(comboW);
    if (ImGui::BeginCombo("##antiplayer", antiPlayerName)) {
        for (s32 pi = 0; pi < playerCount; pi++) {
            struct lobbyplayer_view pv;
            memset(&pv, 0, sizeof(pv));
            if (!lobbyRoomGetPlayerInfo(pi, &pv)) continue;
            bool sel = (pv.clientId == s_CounterOpClientId);
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
                if (matchStart() != 0) {
                    return -1;
                }
                combatSimVerifyOnMatchStart("room");
                /* B-1312: release the Room's slot before closing, as Leave
                 * does: when the Room owns the menu input layer (opened
                 * without the Main Menu's push), only this pops it, and the
                 * match would otherwise run under it (B-1042's class). */
                menupoolRelease(MENU_TYPE_ROOM);
                pdguiSoloRoomClose();
                return 0;
            } else {
                int humanCount = s_IsSoloMode ? 1 : lobbyRoomGetPlayerCount();
                int maxBots = matchConfigMaxBotsForHumans(humanCount);
                int numBots = countBots();
                if (numBots > maxBots) {
                    numBots = maxBots;
                }
                u8 simType  = getLeadSimType();
                /* Backstop for the networked request: the options and mode
                 * read below must already carry the scenario's rules. The
                 * selection handler normally did this; if it changes
                 * anything here, the room settings must be re-sent too. */
                if (matchConfigApplyScenarioRules("room-net-start") > 0) {
                    s_RoomSettingsDirty = true;
                }
                return netLobbyRequestStartWithSims(
                    GAMEMODE_MP,
                    s_Arenas[s_SelectedArena].id,
                    0,
                    0xFF,
                    (u8)numBots,
                    simType,
                    g_MatchConfig.timelimit,
                    matchConfigGetUserOptions(),
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
            if (s_IsSoloMode) {
                /* Offline there is no lobby: the original's one-player
                 * co-op with the Partner choice's simulant buddies, then
                 * close the solo Room as the Combat Simulator start does. */
                if (pdguiRoomStartOfflineCoop(coop_id, (u8)s_CampaignDiff) != 0) {
                    return -1;
                }
                /* B-1312: as the Combat start above. */
                menupoolRelease(MENU_TYPE_ROOM);
                pdguiSoloRoomClose();
                return 0;
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

static void roomResetSharedVariant();

static s32 roomGraphLeaveRoom(void *userdata)
{
    (void)userdata;

    roomResetSharedVariant();
    pdguiRoomClearPlayedVariant();
    s_MatchConfigInited = false;
    s_CodeGenerated = false;
    matchConfigRestoreUserOptions("room leave");

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

static void roomRenderGridModSharing()
{
    if (pdguiCountdownIsActive()) return;
    if (const grid_mod_proposal_control_t *proposal = netmsgGridReviewModProposal()) {
        if (ImGui::SmallButton("Review Mod Request")) ImGui::OpenPopup("##grid_mod_request");
        if (ImGui::BeginPopup("##grid_mod_request")) {
            ImGui::TextUnformatted("Proposed mod packages");
            ImGui::TextWrapped("Approved mods become available in this Room. Players receive the sources used by the pinned level or match.");
            const float width = 400.0f * pdguiScale(1.0f);
            if (ImGui::BeginChild("##proposed_packages", ImVec2(width, ImGui::GetTextLineHeightWithSpacing() * 6),
                    ImGuiChildFlags_Borders)) {
                for (u16 i = 0; i < proposal->packages.count; i++) {
                    const auto &package = proposal->sources.packages[proposal->packages.indexes[i]];
                    char hash[SHA256_HEX_SIZE];
                    sha256ToHex(package.digest, hash);
                    ImGui::Text("%s  %.12s  %u KiB", package.owner.id, hash, (package.size + 1023u) / 1024u);
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", hash);
                }
            }
            ImGui::EndChild();
            if (ImGui::Button("Approve Mod Request") && netmsgGridResolveModProposal(1)) ImGui::CloseCurrentPopup();
            ImGui::SameLine();
            if (ImGui::Button("Decline") && netmsgGridResolveModProposal(0)) ImGui::CloseCurrentPopup();
            if (const char *notice = netmsgGridModProposalNotice()) ImGui::TextWrapped("%s", notice);
            ImGui::EndPopup();
        }
    }
    if (netmsgGridCanProposeMod()) {
        static char filter[64];
        static char chosen[CATALOG_ID_LEN];
        if (ImGui::SmallButton("Propose a Mod")) ImGui::OpenPopup("##grid_mod_propose");
        if (ImGui::BeginPopup("##grid_mod_propose")) {
            ImGui::InputTextWithHint("##grid_mod_filter", "Search installed mods", filter, sizeof(filter));
            const float width = 365.0f * pdguiScale(1.0f);
            if (ImGui::BeginChild("##propose_assets", ImVec2(width, ImGui::GetTextLineHeightWithSpacing() * 8),
                    ImGuiChildFlags_Borders)) {
                const s32 count = assetCatalogGetPoolSize();
                for (s32 i = 0; i < count; i++) {
                    const asset_entry_t *entry = assetCatalogGetByIndex(i);
                    if (!entry || !entry->enabled || entry->bundled || entry->temporary || entry->type == ASSET_ARENA
                            || (filter[0] && !strstr(entry->id, filter))) continue;
                    char descriptor[FS_MAXPATH + 1], archive[FS_MAXPATH + 1], member[FS_MAXPATH + 1];
                    if (!gridMatchCatalogArchiveDescriptor(entry, descriptor, sizeof(descriptor))
                            || !gridMatchPackageLocation(descriptor, archive, sizeof(archive), member, sizeof(member), nullptr)
                            || member[0] || !gridMatchPackageTypeShareable(entry->type, strrchr(archive, '.'))) continue;
                    if (ImGui::Selectable(entry->id, strcmp(entry->id, chosen) == 0)) {
                        snprintf(chosen, sizeof(chosen), "%s", entry->id);
                    }
                }
            }
            ImGui::EndChild();
            ImGui::BeginDisabled(!chosen[0]);
            const bool propose = ImGui::Button("Propose and Allow Sharing");
            ImGui::EndDisabled();
            if (propose && netmsgGridProposeMod(chosen)) ImGui::CloseCurrentPopup();
            if (const char *notice = netmsgGridModProposalNotice()) ImGui::TextWrapped("%s", notice);
            ImGui::EndPopup();
        }
    }
    if (const char *notice = netmsgGridModProposalNotice()) ImGui::TextWrapped("%s", notice);
}

static void roomShowDraftSource(const grid_edit_room_invite_t &invite)
{
    ImGui::TextWrapped("Arena %s", invite.arena_id);
    ImGui::Text("Source %.12s", invite.arena_sha256);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", invite.arena_sha256);
}

void pdguiDrawGridPlaytestMap(ImDrawList *draw, const grid_playtest_view_t &view,
    ImVec2 origin, float side, const u16 *creatorCursor);

static void roomRenderGridPlaytestView(const grid_playtest_view_t &view)
{
    ImGui::SeparatorText("Live Playtest");
    grid_edit_room_control_t layout = {};
    if (netGridEditGetObservedLayout(&layout)) {
        char digest[SHA256_HEX_SIZE];
        sha256ToHex(layout.review.arena_sha256, digest);
        ImGui::TextWrapped("Layout %s", layout.review.arena_id);
        ImGui::Text("Layout source %.12s", digest);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", digest);
    }
    const float side = std::min(ImGui::GetContentRegionAvail().x, pdguiScale(165.0f));
    if (side <= 0.0f) return;
    pdguiDrawGridPlaytestMap(ImGui::GetWindowDrawList(), view, ImGui::GetCursorScreenPos(), side, nullptr);
    ImGui::Dummy(ImVec2(side, side));
    ImGui::TextDisabled("Circle: player   Square: bot");
}

extern "C" void pdguiRenderGridPlaytestVisibility(s32 livePlaytest)
{
    if (livePlaytest && !netGridEditPrivatePlaytestRoom(g_NetMatchRoomId)) return;
    s32 players, creators;
    if (!netGridEditGetPlaytestVisibility(&players, &creators)) return;

    if (livePlaytest) {
        ImGui::Spacing();
        ImGui::TextDisabled("%s Playtest Visibility", forgeDisplayName());
    }
    bool showCursor = players != 0;
    bool showMatch = creators != 0;
    const bool cursorChanged = ImGui::Checkbox("Players See Creator Cursor", &showCursor);
    const bool matchChanged = ImGui::Checkbox("Creator Sees Match", &showMatch);
    if (cursorChanged || matchChanged) netGridEditSetPlaytestVisibility(showCursor, showMatch);
}

static s32 roomCollectGridDraftPeers(lobbyplayer_view peers[HUB_MAX_CLIENTS], s32 roomOnly)
{
    s32 peerCount = 0;
    const s32 count = roomOnly ? lobbyRoomGetPlayerCount() : lobbyGetPlayerCount();
    for (s32 i = 0; i < count && peerCount < HUB_MAX_CLIENTS; ++i) {
        lobbyplayer_view player = {};
        if (!lobbyProjectViewIndex(i, roomOnly, &player) || !player.active || player.isLocal) continue;
        const s32 canInvite = roomOnly ? netGridEditCanInvite(player.clientId)
            : netGridEditCanInviteToResumeRoom(player.clientId);
        if (canInvite) peers[peerCount++] = player;
    }
    return peerCount;
}

static void roomRenderGridResumeInvites(const grid_edit_room_invite_t &invite,
    const lobbyplayer_view *peers, s32 peerCount)
{
    for (s32 i = 0; i < peerCount; ++i) {
        ImGui::PushID(peers[i].clientId);
        char label[sizeof(peers[i].name) + sizeof("Invite  to Resume###room_resume_invite")];
        snprintf(label, sizeof(label), "Invite %s to Resume###room_resume_invite", peers[i].name);
        if (ImGui::Button(label)) netGridEditInvite(peers[i].clientId, invite.arena_id, invite.arena_sha256);
        ImGui::PopID();
    }
}

static void roomRenderStagedReview(bool host)
{
    grid_edit_variant_review_info_t review = {};
    s32 has_review = netGridEditGetStagedReview(&review);
    if (host && netGridEditCanOfferStagedReview()) {
        const char *label = has_review ? "Retry Dependency Review" : "Send to Creator for Review";
        if (ImGui::Button(label) && netGridEditOfferStagedReview()) {
            has_review = netGridEditGetStagedReview(&review);
        }
        if (netGridEditCanOfferStagedReviewTransfer()) {
            if (ImGui::Button("Offer Arena for Temporary Review")
                && netGridEditOfferStagedReviewTransfer()) {
                has_review = netGridEditGetStagedReview(&review);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Lets the invited creator receive the exact staged arena for this review after they agree.");
            }
        }
    }
    if (!has_review) return;
    if (!host) {
        ImGui::SeparatorText("Staged Variant Review");
        char digest[SHA256_HEX_SIZE];
        sha256ToHex(review.variant.arena_sha256, digest);
        ImGui::TextWrapped("%s  %.12s", review.variant.arena_id, digest);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", digest);
        ImGui::Text("Extend Room %u by %.2f m", (unsigned)review.variant.edited_room, review.variant.extension / 100.0);
    }
    if (review.transfer_active && review.arena_bytes) {
        ImGui::TextUnformatted(host ? "Sending arena for review" : "Receiving arena for review");
        char progress[48];
        snprintf(progress, sizeof(progress), "%u / %u bytes", review.transferred_bytes, review.arena_bytes);
        ImGui::ProgressBar((float)review.transferred_bytes / review.arena_bytes, ImVec2(-1, 0), progress);
        if (host && review.transferred_bytes == review.arena_bytes) {
            ImGui::TextDisabled("Waiting for the creator to verify the source packages.");
        }
        if (ImGui::Button("Cancel Arena Review")) netGridEditCancelStagedReview();
        return;
    }
    if (host) {
        switch (review.reply) {
            case GRID_EDIT_VARIANT_REVIEW_DECLINED: ImGui::TextDisabled("Creator declined the review."); break;
            case GRID_EDIT_VARIANT_REVIEW_ACKNOWLEDGED:
                ImGui::TextDisabled("Creator acknowledged the proposal. Their arena is unverified.");
                break;
            case GRID_EDIT_VARIANT_REVIEW_SOURCE_VERIFIED:
                ImGui::TextDisabled(review.includes_dependencies
                    ? "Creator verified the arena. Dependencies are unverified."
                    : "Creator verified the local arena.");
                break;
            case GRID_EDIT_VARIANT_REVIEW_DEPS_VERIFIED:
                if (review.match_sources_prepared) {
                    ImGui::TextDisabled("Arena and private match sources prepared.");
                    if (review.apply_pending) {
                        ImGui::TextDisabled("Waiting for the creator to confirm the layout.");
                    } else if (netGridEditCanApplyStagedLayout()
                            && ImGui::Button("Apply to Running Playtest")) {
                        netGridEditApplyStagedLayout();
                    }
                } else {
                    ImGui::TextDisabled(review.sources_prepared
                        ? "Host and creator prepared the exact arena source packages."
                        : "Creator verified the arena and matched its dependencies.");
                    if (review.match_preparation_issue[0]) ImGui::TextWrapped("%s", review.match_preparation_issue);
                }
                break;
            default:
                ImGui::TextDisabled(review.transfer_offered
                    ? "Waiting for the creator to review or accept the arena." : "Waiting for creator review.");
                break;
        }
        if (!review.reply && review.transfer_offered && ImGui::Button("Cancel Arena Review")) {
            netGridEditCancelStagedReview();
        }
        return;
    }

    if (!review.reply) {
        if (review.transfer_available) {
            ImGui::TextWrapped("Receive %u bytes from the room host. The arena stays in memory for this review.", review.arena_bytes);
            if (ImGui::Button("Receive Arena for Review")) netGridEditAcceptStagedReviewTransfer();
        }
        if (ImGui::Button("Acknowledge Variant")) netGridEditReplyToStagedReview(1);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(review.includes_dependencies
            ? "Checks the exact installed arena and compares its dependencies."
            : "Checks the exact arena if you have it installed.");
        ImGui::SameLine();
        if (ImGui::Button("Decline Variant")) netGridEditReplyToStagedReview(0);
    } else if (review.reply == GRID_EDIT_VARIANT_REVIEW_DEPS_VERIFIED) {
        if (review.apply_pending) {
            ImGui::TextDisabled("Layout ready. Waiting for the host to apply it.");
        } else {
            ImGui::TextDisabled(review.sources_prepared ? "Exact source packages prepared for this review."
                : "Local arena verified. Dependencies match.");
        }
    } else if (review.reply == GRID_EDIT_VARIANT_REVIEW_SOURCE_VERIFIED) {
        ImGui::TextDisabled(review.includes_dependencies
            ? "Local arena verified. Dependencies are unverified." : "Local arena verified.");
    } else if (review.reply == GRID_EDIT_VARIANT_REVIEW_ACKNOWLEDGED) {
        ImGui::TextDisabled("Proposal acknowledged. Local arena unverified.");
    } else {
        ImGui::TextDisabled("Review declined.");
    }
}

extern "C" void pdguiRenderGridRoomExtension(void)
{
    if (!netGridEditCanStageRoomExtension()) return;
    ImGui::SeparatorText("Extend a Room");
    ImGui::PushID("grid-room-extension");
    static char name[64];
    static int room;
    static int metres = 3;
    ImGui::InputText("Variant name", name, sizeof(name));
    const char *const rooms[] = {"Room 1", "Room 2"};
    ImGui::Combo("Room", &room, rooms, 2);
    ImGui::InputInt("Extra length (m)", &metres);
    constexpr int maxMetres = (ForgeRoomSize::MaxLength - ForgeRoomSize::MinLength) / 100;
    ImGui::BeginDisabled(!name[0] || metres < 1 || metres > maxMetres);
    if (ImGui::Button("Stage Extended Variant")) {
        char arena[CATALOG_ID_LEN] = {};
        if (netGridEditStageRoomExtension(name, room + 1, metres * 100, arena, sizeof(arena))) name[0] = '\0';
    }
    ImGui::EndDisabled();
    grid_edit_staged_variant_info_t staged;
    if (netGridEditGetStagedVariant(&staged)) {
        ImGui::TextWrapped("Staged %s  %.12s", staged.arena_id, staged.arena_sha256);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", staged.arena_sha256);
        if (staged.draft_revision != staged.current_revision) {
            ImGui::TextWrapped("Staged from draft revision %llu. Current draft is revision %llu.",
                (unsigned long long)staged.draft_revision, (unsigned long long)staged.current_revision);
        }
        if (staged.applied_layout_generation) ImGui::TextDisabled("Layout applied to this running playtest.");
        roomRenderStagedReview(true);
    }
    const char *error = netGridEditError();
    if (error && error[0]) ImGui::TextWrapped("%s", error);
    ImGui::PopID();
}

extern "C" void pdguiRenderGridPlaytestResume(void)
{
    grid_edit_room_state_t draft = {};
    if (!netGridEditPrivatePlaytestRoom(g_NetMatchRoomId) || !netGridEditGetState(&draft)
            || !draft.is_host || !draft.creator_detached
            || !netGridEditPrivateResumeRoomOpen(g_NetMatchRoomId)) return;

    ImGui::Spacing();
    ImGui::TextDisabled("Shared Draft");
    if (draft.pending_resume) {
        ImGui::TextWrapped("Waiting for the invited creator to verify the draft.");
    } else {
        lobbyplayer_view peers[HUB_MAX_CLIENTS];
        const s32 roomPeerCount = roomCollectGridDraftPeers(peers, 1);
        roomRenderGridResumeInvites(draft.invite, peers, roomPeerCount);
        const s32 outsidePeerCount = roomCollectGridDraftPeers(peers, 0);
        for (s32 i = 0; i < outsidePeerCount; ++i) {
            ImGui::PushID(peers[i].clientId);
            char label[sizeof(peers[i].name) + sizeof("Invite  to Room###grid_resume_room_invite")];
            snprintf(label, sizeof(label), "Invite %s to Room###grid_resume_room_invite", peers[i].name);
            if (ImGui::Button(label)) netGridEditInviteToResumeRoom(peers[i].clientId);
            ImGui::PopID();
        }
        if (!roomPeerCount && !outsidePeerCount)
            ImGui::TextWrapped("The creator can rejoin this Room to resume the draft.");
    }
    const char *error = netGridEditError();
    if (error && error[0]) ImGui::TextWrapped("%s", error);
}

static bool sameDraftValue(const grid_edit_ambient_light_t &a, const grid_edit_ambient_light_t &b)
{
    return a.enabled == b.enabled && a.brightness == b.brightness;
}

static bool sameDraftValue(const grid_edit_move_spawn_t &a, const grid_edit_move_spawn_t &b)
{
    return std::equal(a.position, a.position + 3, b.position) && std::equal(a.look, a.look + 3, b.look);
}

static bool sameDraftValue(const grid_edit_move_decoration_t &a, const grid_edit_move_decoration_t &b)
{
    return a.x == b.x && a.z == b.z;
}

template <typename Value>
struct SharedDraftInput {
    Value baseline = {};
    Value edited = {};
    u64 submittedSequence = 0;
    bool conflict = false;

    bool dirty() const { return !sameDraftValue(baseline, edited); }

    void reload(const Value &current)
    {
        baseline = edited = current;
        submittedSequence = 0;
        conflict = false;
    }

    void sync(const Value &current, const grid_edit_room_state_t &draft, bool selectionChanged)
    {
        /* The local sequence advances only on a matching commit. Read back the
         * actual value in case the host normalized it. */
        const bool committed = submittedSequence && draft.next_sequence != submittedSequence;
        if (selectionChanged || committed || (!conflict && !dirty())) {
            reload(current);
            return;
        }
        if (!draft.pending_command) submittedSequence = 0;
        if (!sameDraftValue(baseline, current)) conflict = true;
    }
};

static void roomRenderSharedAmbient(const grid_edit_room_state_t &draft, bool newDraft, grid_edit_resource_t &focus)
{
    static SharedDraftInput<grid_edit_ambient_light_t> input;
    const grid_edit_ambient_light_t current = { draft.enabled, draft.brightness };
    input.sync(current, draft, newDraft);

    if (input.conflict) {
        ImGui::TextWrapped("Ambient light changed in the shared draft. Reload it before applying.");
        if (ImGui::Button("Reload Ambient Light")) input.reload(current);
    }
    bool enabled = input.edited.enabled != 0;
    if (ImGui::Checkbox("Ambient light override", &enabled)) input.edited.enabled = enabled;
    ImGui::SliderInt("Brightness", &input.edited.brightness, 0, 255, "%d", ImGuiSliderFlags_AlwaysClamp);
    if (ImGui::IsItemActive() && !draft.pending_command) focus.kind = GRID_EDIT_RESOURCE_AMBIENT_LIGHT;

    const bool dirty = input.dirty();
    ImGui::BeginDisabled(input.conflict || !dirty);
    if (ImGui::Button("Apply Ambient Light") && netGridEditSetAmbient(input.edited.enabled, input.edited.brightness)) {
        input.submittedSequence = draft.next_sequence;
    }
    ImGui::EndDisabled();
    if (draft.can_undo_ambient) {
        ImGui::SameLine();
        ImGui::BeginDisabled(input.conflict || dirty);
        if (ImGui::Button("Undo Ambient Edit")) {
            netGridEditSetAmbient(draft.previous_ambient.enabled, draft.previous_ambient.brightness);
        }
        ImGui::EndDisabled();
    }
}

struct RoomLivePlanTarget {
    grid_edit_resource_t resource = {};
    grid_playtest_target_t layout = {};
    u64 revision = 0;
    f32 x = 0, z = 0;
};

static bool roomPlanContains(const grid_playtest_view_t &view, f32 x, f32 z)
{
    return std::isfinite(x) && std::isfinite(z)
        && x >= view.min_x && x <= view.max_x && z >= view.min_z && z <= view.max_z;
}

static bool roomRenderLivePlanTarget(const grid_edit_room_state_t &draft, const grid_playtest_view_t *view,
    const grid_edit_resource_t &resource, bool canTarget, f32 currentX, f32 currentZ,
    RoomLivePlanTarget &plan, grid_edit_resource_t &focus, f32 creatorCursor[3])
{
    const bool spawn = resource.kind == GRID_EDIT_RESOURCE_SPAWN;
    const char *resourceId = spawn ? resource.spawn_id : resource.record_id;
    if (plan.resource.kind == resource.kind
            && (!view || !canTarget || plan.revision != draft.revision
                || plan.layout.epoch != view->epoch || plan.layout.layout_generation != view->layout_generation
                || strcmp(spawn ? plan.resource.spawn_id : plan.resource.record_id, resourceId) != 0)) {
        plan = {};
    }
    if (!view || !canTarget || !roomPlanContains(*view, currentX, currentZ)) return false;

    if (plan.resource.kind != resource.kind) {
        if (ImGui::Button("Target on Live Plan")) {
            plan.resource = resource;
            plan.layout = { view->epoch, view->layout_generation };
            plan.revision = draft.revision;
            plan.x = currentX;
            plan.z = currentZ;
            focus = resource;
            creatorCursor[0] = plan.x;
            creatorCursor[2] = plan.z;
        }
        return false;
    }

    const float side = std::min(ImGui::GetContentRegionAvail().x, pdguiScale(225.0f));
    const float inset = pdguiScale(6.0f);
    if (side <= inset * 2.0f) return false;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float extent = side - inset * 2.0f;
    ImGui::InvisibleButton("##live_plan_canvas", ImVec2(side, side));
    if (ImGui::IsItemClicked()) {
        const ImVec2 pointer = ImGui::GetIO().MousePos;
        const auto world = [&](float pixel, float start, f32 minimum, f32 maximum) {
            const double fraction = std::clamp((pixel - start - inset) / extent, 0.0f, 1.0f);
            return static_cast<f32>(minimum + fraction * (static_cast<double>(maximum) - minimum));
        };
        plan.x = world(pointer.x, origin.x, view->min_x, view->max_x);
        plan.z = world(pointer.y, origin.y, view->min_z, view->max_z);
    }
    pdguiDrawGridPlaytestMap(ImGui::GetWindowDrawList(), *view, origin, side, nullptr);
    const auto screen = [&](f32 x, f32 z) {
        return ImVec2(origin.x + inset + extent * ((x - view->min_x) / (view->max_x - view->min_x)),
            origin.y + inset + extent * ((z - view->min_z) / (view->max_z - view->min_z)));
    };

    ImGui::SliderFloat("Plan X", &plan.x, view->min_x, view->max_x, "%.1f",
        ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoInput);
    ImGui::SliderFloat("Plan Z", &plan.z, view->min_z, view->max_z, "%.1f",
        ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoInput);
    const ImVec2 current = screen(currentX, currentZ);
    const ImVec2 preview = screen(plan.x, plan.z);
    const float marker = pdguiScale(5.0f);
    ImDrawList *draw = ImGui::GetWindowDrawList();
    draw->AddCircle(current, marker, IM_COL32(219, 231, 239, 255));
    draw->AddLine(ImVec2(preview.x - marker, preview.y), ImVec2(preview.x + marker, preview.y),
        IM_COL32(91, 224, 203, 255), 2.0f);
    draw->AddLine(ImVec2(preview.x, preview.y - marker), ImVec2(preview.x, preview.y + marker),
        IM_COL32(91, 224, 203, 255), 2.0f);
    ImGui::TextDisabled("Ring: current   Cross: preview");
    if (spawn) ImGui::TextWrapped("Player starts change in the draft for a later playtest.");

    const bool moved = plan.x != currentX || plan.z != currentZ;
    ImGui::BeginDisabled(!moved);
    const bool submit = ImGui::Button("Apply Plan Target");
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel Target")) {
        plan = {};
        return false;
    }
    focus = resource;
    creatorCursor[0] = plan.x;
    creatorCursor[2] = plan.z;
    return submit;
}

static bool roomRenderSharedSpawns(const grid_edit_room_state_t &draft, bool newDraft,
    const grid_playtest_view_t *view, RoomLivePlanTarget &plan,
    f32 creatorCursor[3], grid_edit_resource_t &focus)
{
    static s32 selected;
    static SharedDraftInput<grid_edit_move_spawn_t> input;
    grid_edit_move_spawn_t current = {};
    const s32 count = netGridEditSpawnCount();
    if (newDraft || selected < 0 || selected >= count) selected = 0;
    if (count <= 0 || !netGridEditGetSpawnPose(selected, &current)) {
        input.reload({});
        if (plan.resource.kind == GRID_EDIT_RESOURCE_SPAWN) plan = {};
        return false;
    }
    input.sync(current, draft, newDraft || strcmp(input.baseline.spawn_id, current.spawn_id) != 0);

    ImGui::SeparatorText("Player Starts");
    if (ImGui::BeginCombo("Start", current.spawn_id)) {
        for (s32 i = 0; i < count; i++) {
            grid_edit_move_spawn_t spawn = {};
            if (netGridEditGetSpawnPose(i, &spawn) && ImGui::Selectable(spawn.spawn_id, i == selected)
                    && i != selected) {
                selected = i;
                current = spawn;
                input.reload(current);
            }
        }
        ImGui::EndCombo();
    }
    ImGui::PushID(current.spawn_id);
    if (input.conflict) {
        ImGui::TextWrapped("This start changed in the shared draft. Reload it before applying.");
        if (ImGui::Button("Reload Start")) input.reload(current);
    }
    grid_edit_move_spawn_t &edited = input.edited;
    ImGui::InputFloat3("Position", edited.position, "%.1f");
    const bool positionActive = ImGui::IsItemActive();
    const bool editingPosition = positionActive && !draft.is_host && !draft.pending_command
        && std::isfinite(edited.position[0]) && std::isfinite(edited.position[1])
        && std::isfinite(edited.position[2]);
    if (editingPosition) memcpy(creatorCursor, edited.position, sizeof(edited.position));
    ImGui::InputFloat3("Facing", edited.look, "%.3f");
    if (!draft.pending_command && (positionActive || ImGui::IsItemActive())) {
        focus.kind = GRID_EDIT_RESOURCE_SPAWN;
        memcpy(focus.spawn_id, edited.spawn_id, sizeof(focus.spawn_id));
    }
    const bool dirty = input.dirty();
    ImGui::BeginDisabled(input.conflict || !dirty);
    if (ImGui::Button("Apply Start Position") && netGridEditMoveSpawn(&edited)) {
        input.submittedSequence = draft.next_sequence;
    }
    ImGui::EndDisabled();
    if (draft.can_undo_spawn) {
        ImGui::SameLine();
        const bool undoSelected = strcmp(current.spawn_id, draft.previous_spawn.spawn_id) == 0;
        ImGui::BeginDisabled(undoSelected && (input.conflict || dirty));
        if (ImGui::Button("Undo Start Edit")) netGridEditMoveSpawn(&draft.previous_spawn);
        ImGui::EndDisabled();
    }
    grid_edit_resource_t resource = {};
    resource.kind = GRID_EDIT_RESOURCE_SPAWN;
    memcpy(resource.spawn_id, current.spawn_id, sizeof(resource.spawn_id));
    if (roomRenderLivePlanTarget(draft, view, resource, !input.conflict && !dirty,
            current.position[0], current.position[2], plan, focus, creatorCursor)) {
        grid_edit_move_spawn_t move = current;
        move.position[0] = plan.x;
        move.position[2] = plan.z;
        if (netGridEditMoveSpawnOnPlan(&move, &plan.layout)) {
            input.submittedSequence = draft.next_sequence;
            plan = {};
        }
    }
    ImGui::PopID();
    return editingPosition || plan.resource.kind == GRID_EDIT_RESOURCE_SPAWN;
}

static bool roomRenderSharedDecorations(const grid_edit_room_state_t &draft, bool newDraft,
    const grid_playtest_view_t *view, RoomLivePlanTarget &plan, f32 cursor[3], grid_edit_resource_t &focus)
{
    static s32 selected;
    static SharedDraftInput<grid_edit_move_decoration_t> input;
    grid_edit_move_decoration_t current = {};
    const s32 count = netGridEditDecorationCount();
    if (newDraft || selected < 0 || selected >= count) selected = 0;
    if (count <= 0 || !netGridEditGetDecorationPose(selected, &current)) {
        input.reload({});
        if (plan.resource.kind == GRID_EDIT_RESOURCE_DECORATION) plan = {};
        return false;
    }
    input.sync(current, draft, newDraft || strcmp(input.baseline.record_id, current.record_id) != 0);

    ImGui::SeparatorText("Decorations");
    if (ImGui::BeginCombo("Decoration", current.record_id)) {
        for (s32 i = 0; i < count; ++i) {
            grid_edit_move_decoration_t pose = {};
            if (netGridEditGetDecorationPose(i, &pose) && ImGui::Selectable(pose.record_id, i == selected)
                    && i != selected) {
                selected = i;
                current = pose;
                input.reload(current);
            }
        }
        ImGui::EndCombo();
    }
    ImGui::PushID(current.record_id);
    if (input.conflict) {
        ImGui::TextWrapped("This decoration changed in the shared draft. Reload it before moving it.");
        if (ImGui::Button("Reload Decoration")) input.reload(current);
    }
    grid_edit_move_decoration_t &edited = input.edited;
    f32 position[2] = { edited.x, edited.z };
    if (ImGui::InputFloat2("X and Z", position, "%.1f")) {
        edited.x = position[0];
        edited.z = position[1];
    }
    const bool positionActive = ImGui::IsItemActive() && !draft.pending_command;
    if (positionActive) {
        focus.kind = GRID_EDIT_RESOURCE_DECORATION;
        memcpy(focus.record_id, edited.record_id, sizeof(focus.record_id));
    }
    const bool creatorCursor = positionActive && !draft.is_host && std::isfinite(edited.x) && std::isfinite(edited.z);
    if (creatorCursor) {
        cursor[0] = edited.x;
        cursor[1] = 0;
        cursor[2] = edited.z;
    }
    const bool dirty = input.dirty();
    ImGui::BeginDisabled(input.conflict || !dirty);
    if (ImGui::Button("Move Decoration") && netGridEditMoveDecoration(&edited)) {
        input.submittedSequence = draft.next_sequence;
    }
    ImGui::EndDisabled();
    if (draft.can_undo_decoration) {
        ImGui::SameLine();
        const bool undoSelected = strcmp(current.record_id, draft.previous_decoration.record_id) == 0;
        ImGui::BeginDisabled(undoSelected && (input.conflict || dirty));
        if (ImGui::Button("Undo Decoration Edit")) netGridEditMoveDecoration(&draft.previous_decoration);
        ImGui::EndDisabled();
    }
    grid_edit_resource_t resource = {};
    resource.kind = GRID_EDIT_RESOURCE_DECORATION;
    memcpy(resource.record_id, current.record_id, sizeof(resource.record_id));
    if (roomRenderLivePlanTarget(draft, view, resource, !input.conflict && !dirty,
            current.x, current.z, plan, focus, cursor)) {
        grid_edit_move_decoration_t move = current;
        move.x = plan.x;
        move.z = plan.z;
        if (netGridEditMoveDecorationOnPlan(&move, &plan.layout)) {
            input.submittedSequence = draft.next_sequence;
            plan = {};
        }
    }
    ImGui::PopID();
    return creatorCursor || plan.resource.kind == GRID_EDIT_RESOURCE_DECORATION;
}

struct room_shared_variant_t {
    grid_edit_wire_identity_t identity = {};
    net_grid_edit_variant_review_t review = {};
    forge_level_doc_t *doc = nullptr;
    grid_match_sources_t sources = {};
    pdgui_grid_variant_picker_t picker;
    char name[64] = {};
    char saved[CATALOG_ID_LEN] = {};
    char localSaveWarning[256] = {};
    char error[256] = {};
};

static room_shared_variant_t s_RoomSharedVariant;

static void roomClearSharedVariantReview()
{
    auto &panel = s_RoomSharedVariant;
    if (panel.doc) forgeLevelDocFree(panel.doc);
    panel.doc = nullptr;
    gridMatchSourcesFree(&panel.sources);
    panel.picker = {};
    panel.review = {};
    panel.error[0] = '\0';
}

static void roomResetSharedVariant()
{
    roomClearSharedVariantReview();
    s_RoomSharedVariant.identity = {};
    s_RoomSharedVariant.name[0] = '\0';
    s_RoomSharedVariant.saved[0] = '\0';
    s_RoomSharedVariant.localSaveWarning[0] = '\0';
}

static void roomRenderSharedVariantSave(const grid_edit_room_state_t *draft)
{
    auto &panel = s_RoomSharedVariant;
    const char *popup = "Save Draft as Variant###room_grid_shared_variant";
    const bool idleDraft = draft && !draft->pending_command && !pdguiCountdownIsActive();
    const bool available = idleDraft && netGridEditCanReviewVariantSources();
    const bool localByor = idleDraft && netGridEditCanSaveLocalByorVariant();
    bool opening = false;
    if (draft) {
        const auto &identity = draft->invite.identity;
        if (panel.identity.room_id != identity.room_id || panel.identity.generation != identity.generation) {
            roomResetSharedVariant();
            panel.identity = identity;
        }
        ImGui::Separator();
        ImGui::InputText("Variant name", panel.name, sizeof(panel.name));
        ImGui::BeginDisabled((!available && !localByor) || !panel.name[0]);
        const bool save = ImGui::Button("Save Draft as Variant");
        ImGui::EndDisabled();
        if (save && localByor) {
            if (netGridEditSaveLocalByorVariant(panel.name, panel.saved, sizeof(panel.saved))) {
                panel.name[0] = '\0';
                snprintf(panel.localSaveWarning, sizeof(panel.localSaveWarning), "%s", netGridEditError());
            }
        } else if (save && available) {
            roomClearSharedVariantReview();
            if (netGridEditReviewVariantSources(&panel.review, &panel.doc, &panel.sources)) {
                pdguiBeginGridVariantPicker(panel.picker, panel.doc, panel.sources);
                ImGui::OpenPopup(popup);
                opening = true;
            }
        }
        if (panel.saved[0]) ImGui::TextWrapped("Saved variant %s", panel.saved);
        if (panel.localSaveWarning[0] && strcmp(panel.localSaveWarning, netGridEditError()) != 0) {
            ImGui::PushStyleColor(ImGuiCol_Text, pdguiVec4TextWarning(255));
            ImGui::TextWrapped("%s", panel.localSaveWarning);
            ImGui::PopStyleColor();
        }
    }
    if (!panel.doc && !ImGui::IsPopupOpen(popup)) return;

    ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0),
        ImVec2(pdguiScale(690.0f), ImGui::GetIO().DisplaySize.y * 0.8f));
    if (!ImGui::BeginPopupModal(popup, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
        roomClearSharedVariantReview();
        return;
    }
    if (opening) pdguiNavSuppressOpeningGesture();

    bool close = !available || !panel.doc || !netGridEditVariantReviewMatches(&panel.review);
    if (!close) {
        char err[256] = {};
        pdgui_grid_variant_selection_t selection;
        ImGui::Text("Variant %s", panel.name);
        const bool valid = pdguiDrawGridVariantPicker(panel.picker, panel.doc, panel.sources,
            selection, err, sizeof(err));
        if (!valid) ImGui::TextWrapped("%s", err);
        if (panel.error[0]) ImGui::TextWrapped("%s", panel.error);
        ImGui::BeginDisabled(!valid || !panel.name[0]);
        const bool save = ImGui::Button("Save Variant");
        ImGui::EndDisabled();
        ImGui::SameLine();
        const bool cancel = ImGui::Button("Cancel");
        if (opening) ImGui::SetItemDefaultFocus();
        close = cancel || (!opening && pdguiMenuCancelPressed());
        if (save && !close) {
            if (netGridEditSaveSelectedVariant(&panel.review, panel.name, &panel.sources,
                    selection.omitted.data(), selection.omitted.size(), selection.fallbacks.data(),
                    selection.fallbacks.size(), panel.saved, sizeof(panel.saved))) {
                panel.name[0] = '\0';
                close = true;
            } else {
                const char *saveError = netGridEditError();
                snprintf(panel.error, sizeof(panel.error), "%s",
                    saveError && saveError[0] ? saveError : "Could not save the shared variant.");
            }
        }
    }
    if (close) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
    if (close) roomClearSharedVariantReview();
}

static void roomRenderSharedDraft(const grid_match_result_t *played)
{
    static bool cursorPublished;
    static bool focusPublished;
    static grid_edit_wire_identity_t editedSession = {};
    static RoomLivePlanTarget plan;
    grid_edit_room_invite_t invite = {};
    grid_edit_room_state_t draft = {};
    const bool invited = netGridEditPendingInvite(&invite) != 0;
    const bool hasDraft = netGridEditGetState(&draft) != 0;
    const bool showVariant = !invited && hasDraft && draft.is_host && (draft.ready || draft.creator_detached);
    if (!hasDraft || !draft.can_edit) editedSession = {};
    const asset_entry_t *arena = assetCatalogResolve(g_MatchConfig.stage_id);
    const bool canUseSelected = arena && arena->type == ASSET_ARENA
        && arena->enabled && !arena->bundled && arena->grid_offer_only
        && arena->ext.arena.load_mode == ARENA_LOADMODE_PLAYABLE;

    lobbyplayer_view peers[HUB_MAX_CLIENTS];
    s32 peerCount = 0;
    const bool choosePeer = hasDraft ? draft.creator_detached && !draft.pending_invite : played || canUseSelected;
    if (!invited && choosePeer && !pdguiCountdownIsActive()) {
        peerCount = roomCollectGridDraftPeers(peers, 1);
    }

    const char *error = netGridEditError();
    if (invited || hasDraft || peerCount || (error && error[0])) {
        grid_edit_variant_review_info_t review;
        const bool needsReview = hasDraft && !draft.is_host && netGridEditGetStagedReview(&review) && !review.reply;
        const char *label = invited ? "Review Draft Invite###room_shared_draft"
            : needsReview ? "Review Staged Variant###room_shared_draft" : "Shared Draft###room_shared_draft";
        if (ImGui::SmallButton(label)) ImGui::OpenPopup("##room_shared_draft_panel");
    }
    ImGui::SetNextWindowSizeConstraints(ImVec2(pdguiScale(315.0f), 0),
        ImVec2(pdguiScale(445.0f), FLT_MAX));
    if (!ImGui::BeginPopup("##room_shared_draft_panel")) {
        if (s_RoomSharedVariant.doc) roomClearSharedVariantReview();
        plan = {};
        if (cursorPublished) {
            netGridEditSetCreatorCursor(nullptr);
            cursorPublished = false;
        }
        if (focusPublished) {
            netGridEditSetResourceFocus(nullptr);
            focusPublished = false;
        }
        return;
    }

    f32 creatorCursor[3] = {};
    bool hasCreatorCursor = false;
    grid_edit_resource_t focus = {};
    ImGui::SeparatorText("Shared Draft");
    if (invited) {
        roomShowDraftSource(invite);
        ImGui::TextWrapped("Accept to open this exact arena revision in the shared draft.");
        if (ImGui::Button("Accept Invite")) netGridEditReply(1);
        ImGui::SameLine();
        if (ImGui::Button("Decline")) netGridEditReply(0);
    } else if (hasDraft) {
        const grid_edit_wire_identity_t &identity = draft.invite.identity;
        roomShowDraftSource(draft.invite);
        if (draft.can_edit) {
            ImGui::Text("Draft revision %llu", (unsigned long long)draft.revision);
            if (draft.is_host && draft.pending_resume) ImGui::TextDisabled("The creator is catching up.");
            grid_playtest_view_t liveView = {};
            const bool observing = draft.ready && !draft.is_host && netGridEditGetPlaytestView(&liveView);
            if (observing) roomRenderGridPlaytestView(liveView);
            if (draft.ready && !draft.is_host) roomRenderStagedReview(false);
            const bool newDraft = editedSession.room_id != identity.room_id
                || editedSession.generation != identity.generation;
            editedSession = identity;
            const grid_playtest_view_t *targetView = observing && liveView.has_world_bounds && !draft.pending_command
                ? &liveView : nullptr;
            if (newDraft || !targetView) plan = {};
            ImGui::BeginDisabled(draft.pending_command != 0);
            roomRenderSharedAmbient(draft, newDraft, focus);
            hasCreatorCursor = roomRenderSharedSpawns(draft, newDraft, targetView, plan, creatorCursor, focus);
            if (roomRenderSharedDecorations(draft, newDraft, targetView, plan, creatorCursor, focus)) {
                hasCreatorCursor = true;
            }
            ImGui::EndDisabled();
            if (!draft.ready) {
                hasCreatorCursor = false;
                focus = {};
            }
            if (draft.pending_command) ImGui::TextDisabled("Waiting for the host to apply the edit.");
        } else if (draft.pending_invite) {
            ImGui::TextDisabled(draft.pending_resume ? "Waiting for the invited creator to verify the draft."
                : "Waiting for the invited player.");
        } else if (draft.creator_detached) {
            ImGui::Text("Draft revision %llu", (unsigned long long)draft.revision);
            ImGui::TextWrapped("The creator left. You can save the committed draft as a Variant.");
            roomRenderGridResumeInvites(draft.invite, peers, peerCount);
        } else {
            ImGui::TextDisabled("Waiting for the shared draft to open.");
        }
        if (showVariant) {
            roomRenderSharedVariantSave(&draft);
            pdguiRenderGridRoomExtension();
            if (draft.ready) {
                pdguiRenderGridPlaytestVisibility(0);
                const s32 bots = countBots();
                const bool canStart = bots > 0 && strcmp(g_MatchConfig.scenario_id, "base:combat") == 0;
                ImGui::BeginDisabled(!canStart);
                if (ImGui::Button("Private Bot Playtest")) {
                    netGridEditStartPrivatePlaytest(bots, getLeadSimType());
                }
                const char *playableVariant = netGridEditSavedVariantId();
                if (playableVariant[0] && netGridEditHasSavedVariant(playableVariant)) {
                    if (ImGui::Button("Play Saved Variant")) {
                        netGridEditStartPrivateVariantPlaytest(playableVariant, bots, getLeadSimType());
                    }
                }
                grid_edit_staged_variant_info_t staged;
                if (netGridEditGetStagedVariant(&staged) && ImGui::Button("Play Staged Variant")) {
                    netGridEditStartPrivateVariantPlaytest(staged.arena_id, bots, getLeadSimType());
                }
                ImGui::EndDisabled();
                if (!canStart && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                    ImGui::SetTooltip("Choose Combat and add at least one bot.");
                }
            }
        }
        if (ImGui::Button(draft.is_host ? "End Draft" : "Leave Draft")) {
            hasCreatorCursor = false;
            focus = {};
            netGridEditClose();
        }
    } else if (peerCount) {
        static bool useLastPlayed = true;
        if (played && canUseSelected) {
            if (ImGui::RadioButton("Last played arena", useLastPlayed)) useLastPlayed = true;
            if (ImGui::RadioButton("Selected arena", !useLastPlayed)) useLastPlayed = false;
        }
        const bool usePlayed = played && (!canUseSelected || useLastPlayed);
        char digest[SHA256_HEX_SIZE];
        const char *arenaId = g_MatchConfig.stage_id;
        if (usePlayed) {
            arenaId = played->arena_id;
            sha256ToHex(played->arena_sha256, digest);
            ImGui::TextWrapped("Arena %s  %.12s", arenaId, digest);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", digest);
        } else {
            ImGui::TextWrapped("Arena %s", arenaId);
        }
        for (s32 i = 0; i < peerCount; i++) {
            ImGui::PushID(peers[i].clientId);
            char label[sizeof(peers[i].name) + sizeof("Invite ###room_shared_draft_invite")];
            snprintf(label, sizeof(label), "Invite %s###room_shared_draft_invite", peers[i].name);
            if (ImGui::Button(label)) netGridEditInvite(peers[i].clientId, arenaId, usePlayed ? digest : nullptr);
            ImGui::PopID();
        }
    }
    if (!showVariant) roomRenderSharedVariantSave(nullptr);
    error = netGridEditError();
    if (error && error[0]) {
        ImGui::PushStyleColor(ImGuiCol_Text, pdguiVec4TextWarning(255));
        ImGui::TextWrapped("%s", error);
        ImGui::PopStyleColor();
    }
    if (ImGui::Button("Close")) {
        plan = {};
        hasCreatorCursor = false;
        focus = {};
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
    if (hasCreatorCursor || cursorPublished) {
        netGridEditSetCreatorCursor(hasCreatorCursor ? creatorCursor : nullptr);
        cursorPublished = hasCreatorCursor;
    }
    if (focus.kind || focusPublished) {
        netGridEditSetResourceFocus(focus.kind ? &focus : nullptr);
        focusPublished = focus.kind != 0;
    }
}

struct room_played_variant_t {
    grid_match_result_t result = {};
    forge_level_doc_t *doc = nullptr;
    grid_match_sources_t sources = {};
    pdgui_grid_variant_picker_t picker;
    char name[MOD_AUTHOR_NAME_MAX] = {};
    char status[256] = {};
};

static room_played_variant_t s_RoomPlayedVariant;

static void roomClearPlayedVariantReview()
{
    auto &panel = s_RoomPlayedVariant;
    forgeLevelDocFree(panel.doc);
    panel.doc = nullptr;
    gridMatchSourcesFree(&panel.sources);
    panel.picker = {};
    panel.name[0] = '\0';
}

extern "C" void pdguiRoomClearPlayedVariant(void)
{
    if (!s_RoomPlayedVariant.result.stage_epoch) return;
    roomClearPlayedVariantReview();
    s_RoomPlayedVariant.result = {};
    s_RoomPlayedVariant.status[0] = '\0';
}

static bool roomPlayedVariantMatches(const grid_match_result_t *played)
{
    const auto &reviewed = s_RoomPlayedVariant.result;
    return played && gridMatchResultMatchesStage(played, &reviewed.offer, reviewed.stage_epoch,
            reviewed.arena_id, reviewed.arena_sha256)
        && played->end_tick == reviewed.end_tick && played->reason == reviewed.reason
        && memcmp(played->score_sha256, reviewed.score_sha256, sizeof(reviewed.score_sha256)) == 0;
}

static void roomRenderPlayedVariant(const grid_match_result_t *played)
{
    auto &panel = s_RoomPlayedVariant;
    const bool available = played && !pdguiCountdownIsActive() && forgeLevelCanReviewPlayedVariantSources(played);
    if ((!available || !roomPlayedVariantMatches(played)) && panel.result.stage_epoch) pdguiRoomClearPlayedVariant();

    const char *popup = "Save Played Variant###room_grid_played_variant";
    bool opening = false;
    if (available && ImGui::SmallButton("Save Played Variant")) {
        pdguiRoomClearPlayedVariant();
        panel.result = *played;
        if (forgeLevelReviewPlayedVariantSources(&panel.result, &panel.doc, &panel.sources,
                panel.status, sizeof(panel.status))) {
            pdguiBeginGridVariantPicker(panel.picker, panel.doc, panel.sources);
            ImGui::OpenPopup(popup);
            opening = true;
        } else if (!panel.status[0]) {
            snprintf(panel.status, sizeof(panel.status), "Could not review this played revision.");
        }
    }
    if (!panel.doc && panel.status[0]) ImGui::TextWrapped("%s", panel.status);

    ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0),
        ImVec2(pdguiScale(690.0f), ImGui::GetIO().DisplaySize.y * 0.8f));
    if (!ImGui::BeginPopupModal(popup, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
        if (panel.doc) roomClearPlayedVariantReview();
        return;
    }
    if (opening) pdguiNavSuppressOpeningGesture();

    bool close = !available || !panel.doc || !roomPlayedVariantMatches(played);
    if (!close) {
        char err[256] = {};
        pdgui_grid_variant_selection_t selection;
        ImGui::InputText("Variant Name", panel.name, sizeof(panel.name));
        if (opening) ImGui::SetItemDefaultFocus();
        const bool valid = pdguiDrawGridVariantPicker(panel.picker, panel.doc, panel.sources,
            selection, err, sizeof(err));
        if (!valid) ImGui::TextWrapped("%s", err);
        if (panel.status[0]) ImGui::TextWrapped("%s", panel.status);
        const bool cancel = !opening && pdguiMenuCancelPressed();
        ImGui::BeginDisabled(!valid || !panel.name[0]);
        const bool save = ImGui::Button("Save Variant");
        ImGui::EndDisabled();
        ImGui::SameLine();
        close = ImGui::Button("Cancel") || cancel;
        if (save && !close) {
            auto *saved = static_cast<mod_author_result_t *>(calloc(1, sizeof(mod_author_result_t)));
            if (saved && forgeLevelSavePlayedVariant(&panel.result, panel.name, &panel.sources,
                    selection.omitted.data(), selection.omitted.size(), selection.fallbacks.data(),
                    selection.fallbacks.size(), saved, err, sizeof(err))) {
                if (err[0]) {
                    snprintf(panel.status, sizeof(panel.status), "%s", err);
                } else {
                    snprintf(panel.status, sizeof(panel.status), "Saved %s to your local levels.", saved->catalog_id);
                }
                close = true;
            } else {
                snprintf(panel.status, sizeof(panel.status), "Could not save. %.220s",
                    !saved ? "Out of memory." : err[0] ? err : "The played revision is unavailable.");
            }
            free(saved);
        }
    }
    if (close) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
    if (close) roomClearPlayedVariantReview();
}

static void roomRenderGridResult(const grid_match_result_t &played)
{
    char digest[SHA256_HEX_SIZE];
    sha256ToHex(played.arena_sha256, digest);
    ImGui::TextDisabled("Last played revision  %s  %.12s", played.arena_id, digest);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", digest);

    const grid_match_scorecard_t *scores = netGridLastMatchScores();
    if (!scores || !scores->count) return;
    if (ImGui::SmallButton("Review Scores")) ImGui::OpenPopup("##grid_result_scores");
    if (!ImGui::BeginPopup("##grid_result_scores")) return;

    const char *reason = "Match ended";
    switch (played.reason) {
        case GRID_MATCH_END_TIME: reason = "Time limit reached"; break;
        case GRID_MATCH_END_SCORE: reason = "Score limit reached"; break;
        case GRID_MATCH_END_TEAM_SCORE: reason = "Team score limit reached"; break;
        case GRID_MATCH_END_MANUAL: reason = "Host ended the match"; break;
        case GRID_MATCH_END_OTHER: break;
    }
    ImGui::TextUnformatted(reason);
    if (ImGui::BeginTable("##grid_scoreboard", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
        ImGui::TableSetupColumn("Player");
        ImGui::TableSetupColumn("Kills");
        ImGui::TableSetupColumn("Deaths");
        ImGui::TableSetupColumn("Score");
        ImGui::TableHeadersRow();
        for (u8 i = 0; i < scores->count; ++i) {
            const grid_match_score_row_t &row = scores->rows[i];
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            if (row.name[0]) ImGui::TextUnformatted(row.name);
            else ImGui::Text("Participant %u", (unsigned)row.host_slot + 1u);
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%d", row.kills);
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%d", (int)row.deaths);
            ImGui::TableSetColumnIndex(3);
            if (row.has_score) ImGui::Text("%d", row.score);
            else ImGui::TextUnformatted("--");
        }
        ImGui::EndTable();
    }
    if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
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
        if (s_NumSpawnWeapons == 0 || s_SpawnWeaponCatalogGeneration != assetCatalogGetGeneration()) {
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
    const bool socialOwnsInput = pdguiFriendsAnySurfaceIsOpen() != 0;
    static bool socialOwnedInput = false;
    const bool restoreRoomFocus = socialOwnedInput && !socialOwnsInput;
    socialOwnedInput = socialOwnsInput;
    if (socialOwnsInput) wflags |= ImGuiWindowFlags_NoInputs;

    if (!ImGui::Begin("##room_interior", nullptr, wflags)) {
        ImGui::End();
        /* S295 F4 leak guard — S300: pool owns the ctx (if it acquired it),
         * release pops it. If solo mode (main menu pushed ctx), the pool
         * slot is in shared mode and release won't pop. */
        menupoolRelease(MENU_TYPE_ROOM);
        return;
    }

    /* Keep child ownership through this whole render, including a modal
     * that closes inside renderPlayerPanel before the Leave button runs. */
    const bool roomPopupOwnedFrame = ImGui::IsPopupOpen(
        nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);

    /* S300: attach ctx every frame (ImGui can reuse ##room_interior without
     * IsWindowAppearing after stage transitions — same class as main menu). */
    menupoolAcquire(MENU_TYPE_ROOM, NULL, &g_CtxImGuiMenu);

    if (!socialOwnsInput && (restoreRoomFocus || ImGui::IsWindowAppearing())) {
        ImGui::FocusWindow(ImGui::GetCurrentWindow(), ImGuiFocusRequestFlags_UnlessBelowModal);
    }
    if (ImGui::IsWindowAppearing()) {
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

    if (!s_IsSoloMode) {
        const grid_match_result_t *played = netGridLastMatchResult();
        if (played) roomRenderGridResult(*played);
        roomRenderPlayedVariant(played);
        roomRenderSharedDraft(played);
        roomRenderGridModSharing();
        contentH -= ImGui::GetCursorPosY() - curY;
        curY = ImGui::GetCursorPosY();
    }

    /* ---- Tab bar ---- */
    ImGui::SetCursorPosY(curY);

    /* In solo mode the local player is always the leader. */
    bool isLeader = s_IsSoloMode || (lobbyIsLocalLeader() != 0);

    static const char *s_TabNames[] = {
        "Combat Simulator", "Campaign", "Counter-Operative"
    };
    static const int s_NumTabs = 3;
    if (s_ActiveTab < 0 || s_ActiveTab >= s_NumTabs) s_ActiveTab = 0;

    /* Tab switching: ACTION_MENU_TAB_PREV/NEXT are bound to PageUp/PageDown
     * and LB/RB, so controller/keyboard rebinds share one path.
     * Use a pending flag so SetSelected only fires for one frame. */
    static s32 s_BumperPendingTab = -1;

    /* Smoke hook (--debug-room-tab): open on that tab through the same
     * pending selection as the bumpers. The tab bar shows it a frame later,
     * so the debug Start Match below waits until it is the tab shown. */
    static s32 s_DebugRoomTabWanted = -1;
    {
        const s32 wanted = bootConsumeDebugRoomTab();

        if (wanted >= 0 && wanted < s_NumTabs) {
            s_ActiveTab = wanted;
            s_BumperPendingTab = wanted;
            s_DebugRoomTabWanted = wanted;
        }
    }

    if (!socialOwnsInput && pdguiMenuTabPrevPressed()) {
        s_ActiveTab--;
        if (s_ActiveTab < 0) s_ActiveTab = s_NumTabs - 1;
        s_BumperPendingTab = s_ActiveTab;
        pdguiPlaySound(PDGUI_SND_SWIPE);
    }
    if (!socialOwnsInput && pdguiMenuTabNextPressed()) {
        s_ActiveTab++;
        if (s_ActiveTab >= s_NumTabs) s_ActiveTab = 0;
        s_BumperPendingTab = s_ActiveTab;
        pdguiPlaySound(PDGUI_SND_SWIPE);
    }

    /* Rule 6 reconciliation -- Q4 (2026-05-03, Mike's directive):
     *
     * The Combat Sim binding spec v2 JSON had Y bound to social on every
     * focusable element. Mike Q4 INVERTS that: Y-Social is restricted to
     * the Main Menu and in-game Pause Menu only -- NOT universal across
     * other menus. On the Combat Sim Room, Y is UNDEFINED. Per Rule 10
     * (skip-empty-bindings), an undefined input is a no-op AND no glyph
     * hint is rendered for it.
     *
     * Therefore: do NOT poll pdguiMenuTertiaryPressed (Y) here, do NOT
     * call pdguiFriendsSocialOpen() from this screen. The Y-Social
     * binding lives on Main Menu (pdgui_menu_mainmenu.cpp) and Pause
     * Menu (pdgui_menu_pausemenu.cpp) screens; rollouts tracked in
     * kanban c087 (Main Menu) and c088 (Pause Menu).
     *
     * If a future spec change re-promotes Y-Social to universal, restore
     * by polling pdguiMenuTertiaryPressed here and gating on
     * !pdguiFriendsSocialIsOpen() (idempotent re-open guard). */

    /* Rule 8 (2026-05-03): LT/RT skip-up / skip-down.
     *
     * Renamed per Mike's Q-A 2026-05-03 from SectionPrev/Next to
     * SkipUp/Down. Per Mike's Q-B (dynamic walker only): in the Combat
     * Sim Room the player panel exposes a per-row team-jump walker
     * (consumed in renderPlayerPanel), and the left settings panel exposes
     * a section walker: Arena -> Scenario -> Limits -> Weapon Set ->
     * Options. Per Mike's Q-C
     * (page-jump fallback): if the focused panel has neither groups nor
     * scroll, LT/RT is no-op; LT/RT NEVER crosses panels (D-pad does).
     * Boundary case: clamp, no wrap. */
    if (!socialOwnsInput && s_ActiveTab == 0) {
        int skipDir = 0;
        if (pdguiMenuSkipUpPressed()) {
            skipDir = -1;
        } else if (pdguiMenuSkipDownPressed()) {
            skipDir = +1;
        }
        if (skipDir != 0) {
            if (s_RoomFocusRegion == ROOM_FOCUS_PLAYERS) {
                s_RoomPlayerSectionJumpPending = skipDir;
            } else if (s_RoomFocusRegion == ROOM_FOCUS_SETTINGS
                       || s_RoomFocusRegion == ROOM_FOCUS_NONE) {
                s_RoomCsSectionJumpPending = skipDir;
            }
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
    }

    ImGui::SameLine(0.0f, pad);

    renderPlayerPanel(rightW, contentH, isLeader);

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

    if (isLeader) {
        float startW = pdguiScale(210.0f);
        /* M-20: consume progressive-focus-pending request from the
         * Scenario combo (see s_StartMatchFocusPending). Placed before the
         * Button so SetKeyboardFocusHere(0) targets the Start Match
         * widget specifically. */
        if (s_StartMatchFocusPending) {
            ImGui::SetKeyboardFocusHere(0);
            s_StartMatchFocusPending = false;
        }
        /* Mike directive 2026-05-18: --debug-auto-start-match consumer.
         * Fires Start Match once on the first Room frame where the
         * leader-only Start Match block runs, so smokes can drive an
         * end-to-end CS match without keyboard nav. The CLI flag is
         * one-shot (see port/src/main.c::bootConsumeDebugAutoStartMatch). */
        extern s32 bootConsumeDebugAutoStartMatch(void);
        /* --debug-room-tab: keep the one-shot until the requested tab is
         * the one shown, so Start Match starts that tab's mode. */
        const bool debugTabPending = s_DebugRoomTabWanted >= 0
            && s_DebugRoomTabWanted != s_ActiveTab;
        /* --debug-room-load-preset / --debug-room-quick-start: the one-shot
         * waits until their setup has gone in (roomDebugSetupRun below). */
        const bool debugSetupPending = roomDebugSetupPending();
        const bool bootAutoStart = !debugTabPending && !debugSetupPending
            && (bootConsumeDebugAutoStartMatch() != 0);
        const bool cycleAutoStart =
            (combatSimVerifyConsumeRoomAutoStart() != 0);
        const bool autoStart = bootAutoStart || cycleAutoStart;
        if (autoStart) {
            sysLogPrintf(LOG_NOTE,
                "ROOM: --debug-auto-start-match -- firing Start Match tab=%d",
                s_ActiveTab);
            s_DebugRoomTabWanted = -1;
        }
        /* Quick Start (Mike 2026-11-21): its setup went in a frame ago, so a
         * networked room's settings are out before the start request. */
        bool quickStartFire = false;
        if (s_QuickStartFireFrames > 0 && --s_QuickStartFireFrames == 0) {
            quickStartFire = s_ActiveTab == 0;
            sysLogPrintf(quickStartFire ? LOG_NOTE : LOG_WARNING,
                "ROOM: Quick Start -- %s Start Match tab=%d",
                quickStartFire ? "firing" : "not firing (not the Combat Simulator tab)",
                s_ActiveTab);
        }
        if (autoStart || quickStartFire || ImGui::Button("Start Match", ImVec2(startW, btnH))) {
            pdguiPlaySound(PDGUI_SND_SELECT);
            menuGraphFireSceneOp(MENU_TYPE_ROOM, "start_match",
                                 roomGraphStartMatch, NULL);
        }
        if (ImGui::IsItemFocused()) {
            roomRememberFocusedRegion(ROOM_FOCUS_FOOTER);
        }
        /* The smoke hooks' preset or Quick Start, on the Combat Simulator tab;
         * a debug Start Match or Quick Start's own fires on a later frame. */
        if (debugSetupPending && !debugTabPending && s_ActiveTab == 0) {
            roomDebugSetupRun();
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
    const bool roomChildBlocks = roomPopupOwnedFrame || socialOwnsInput || s_ShowChangeCharModal
        || s_BotModalOpen || s_ShowSaveScenario || s_ShowLoadScenario
        || ImGui::IsPopupOpen(nullptr,
            ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
    if ((ImGui::Button(leaveLabel, ImVec2(leaveW, btnH)) && !roomChildBlocks) ||
        (!countdownBlocks && !hotswapBlocks && !roomChildBlocks &&
         pdguiMenuCancelPressed())) {
        if (!s_ShowLeaveConfirm) {
            s_ShowLeaveConfirm = true;
            pdguiPlaySound(PDGUI_SND_SUBFOCUS);
        }
    }
    if (ImGui::IsItemFocused()) {
        roomRememberFocusedRegion(ROOM_FOCUS_FOOTER);
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

            /* Public profile source. Selecting a profile derives the runtime
             * type/difficulty and its default character from one catalog ID. */
            rebuildRoomBotProfileCache();
            ImGui::Text("Profile:");
            ImGui::SameLine(labelCol);
            ImGui::SetNextItemWidth(-1);
            if (ImGui::BeginCombo("##botmodalprofile",
                                  roomBotProfileName(sl->profile_id))) {
                for (s32 pi = 0; pi < s_RoomBotProfileCount; pi++) {
                    const RoomBotProfileEntry &profile =
                        s_RoomBotProfiles[pi];
                    bool selected = strcmp(sl->profile_id, profile.id) == 0;
                    char label[CATALOG_ID_LEN + 112];
                    snprintf(label, sizeof(label), "%s##modal_profile_%s",
                             profile.name, profile.id);
                    if (ImGui::Selectable(label, selected)) {
                        if (matchConfigSetBotProfile(s_EditBotSlotIdx,
                                                     profile.id) == 0) {
                            s_RoomSettingsDirty = true;
                            pdguiPlaySound(PDGUI_SND_SUBFOCUS);
                        }
                    }
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            /* Difficulty */
            ImGui::Text("Difficulty:");
            ImGui::SameLine(labelCol);
            ImGui::SetNextItemWidth(-1);
            if (ImGui::BeginCombo("##botmodaldiff",
                                   s_SimDiffNames[sl->botDifficulty])) {
                for (int d = 0; d < s_NumSimDiffs; d++) {
                    bool sel = (d == (int)sl->botDifficulty);
                    if (ImGui::Selectable(s_SimDiffNames[d], sel)) {
                        if (matchConfigSetBotTraits(s_EditBotSlotIdx,
                                                   sl->botType, (u8)d) == 0) {
                            s_RoomSettingsDirty = true;
                            pdguiPlaySound(PDGUI_SND_SUBFOCUS);
                        }
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

            if (ImGui::Button("Done", ImVec2(pdguiScale(120.0f), 0.0f)) ||
                pdguiMenuCancelPressed()) {
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
            if (ImGui::Button("Close") || pdguiMenuCancelPressed()) {
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
        const bool cancelSaveScenario = pdguiMenuCancelPressed();
        ImGui::TextColored(pdguiVec4TitleGlow(), "Save Scenario");
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text("Name:");
        ImGui::SetNextItemWidth(pdguiScale(420.0f));
        ImGui::InputText("##savename", s_SaveNameBuf, sizeof(s_SaveNameBuf));

        ImGui::Spacing();

        float bw = pdguiScale(150.0f);
        if (ImGui::Button("Save", ImVec2(bw, 0.0f)) && !cancelSaveScenario) {
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
        if (ImGui::Button("Cancel", ImVec2(bw, 0.0f)) || cancelSaveScenario) {
            pdguiPlaySound(PDGUI_SND_KBCANCEL);
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    /* ---- Quick Start popup (Mike 2026-11-21: the original Combat Simulator
     * menu's Quick Start: a Quick Team choice, its simulants, then the match) ---- */
    if (s_ShowQuickStart) {
        ImGui::OpenPopup("Quick Start##quickstartpop");
        s_ShowQuickStart = false;
    }

    ImGui::SetNextWindowSize(ImVec2(pdguiScale(480.0f), 0.0f));
    if (ImGui::BeginPopupModal("Quick Start##quickstartpop", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        const bool cancelQuickStart = pdguiMenuCancelPressed();
        ImGui::TextColored(pdguiVec4TitleGlow(), "Quick Start");
        ImGui::Separator();
        ImGui::Spacing();

        /* Choices that set every player's team wait online (D-056). */
        if (!roomQuickStartModeAvailable(s_QuickStartMode)) {
            s_QuickStartMode = ROOM_QS_PLAYERS_AND_SIMS;
        }
        for (int m = 0; m < ROOM_QS_MODE_COUNT; m++) {
            const bool available = roomQuickStartModeAvailable(m);
            if (!available) ImGui::BeginDisabled();
            if (ImGui::RadioButton(s_QuickStartModeNames[m], s_QuickStartMode == m)) {
                s_QuickStartMode = m;
                pdguiPlaySound(PDGUI_SND_SUBFOCUS);
            }
            if (!available) ImGui::EndDisabled();
            if (m == ROOM_QS_PLAYERS_AND_SIMS) {
                ImGui::Separator();
            }
        }
        if (roomQuickStartRoomHasRemoteHumans()) {
            ImGui::TextDisabled("Players vs. Simulants and Player-Simulant Teams set every");
            ImGui::TextDisabled("player's team, which the host cannot do online yet.");
        }

        if (roomQuickStartModeHasSims(s_QuickStartMode)) {
            const int maxSims = roomQuickStartMaxSims(s_QuickStartMode);
            if (s_QuickStartSims > maxSims) s_QuickStartSims = maxSims;
            if (s_QuickStartSims < 1) s_QuickStartSims = 1;
            ImGui::Spacing();
            ImGui::SetNextItemWidth(pdguiScale(260.0f));
            ImGui::SliderInt(s_QuickStartMode == ROOM_QS_PLAYER_SIM_TEAMS
                                 ? "Simulants per player" : "Simulants",
                             &s_QuickStartSims, 1, maxSims);
            ImGui::SetNextItemWidth(pdguiScale(260.0f));
            if (ImGui::BeginCombo("Difficulty##qsdiff",
                                  s_SimDiffNames[s_QuickStartDiff])) {
                for (int d = 0; d < s_NumSimDiffs; d++) {
                    if (ImGui::Selectable(s_SimDiffNames[d], d == s_QuickStartDiff)) {
                        s_QuickStartDiff = d;
                    }
                    if (d == s_QuickStartDiff) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
        }

        ImGui::Spacing();
        float qbw = pdguiScale(150.0f);
        if (ImGui::Button("Start", ImVec2(qbw, 0.0f)) && !cancelQuickStart) {
            roomQuickStartBegin(s_QuickStartMode, s_QuickStartSims, s_QuickStartDiff);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(qbw, 0.0f)) || cancelQuickStart) {
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
        const bool cancelLoadScenario = pdguiMenuCancelPressed();
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
                    if (ImGui::IsMouseDoubleClicked(0) && !cancelLoadScenario) {
                        /* Double-click: load and close */
                        s32 humanCount = lobbyRoomGetPlayerCount();
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
        if (ImGui::Button("Load", ImVec2(fbw, 0.0f)) && !cancelLoadScenario) {
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
            s32 humanCount = lobbyRoomGetPlayerCount();
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
        if (ImGui::Button("Delete", ImVec2(fbw, 0.0f)) && !cancelLoadScenario) {
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
        if (ImGui::Button("Close", ImVec2(fbw, 0.0f)) || cancelLoadScenario) {
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

                if (forceFocus) {
                    ImGui::SetKeyboardFocusHere(0);
                    ImGui::SetNavCursorVisible(true);
                }
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
                    if (pdguiMenuCancelPressed()) {
                        doCancel = true;
                    }
                }

                if (doConfirm && !doCancel) {
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
            if (forceFocus) {
                ImGui::SetKeyboardFocusHere(0);
                ImGui::SetNavCursorVisible(true);
            }
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
                if (pdguiMenuCancelPressed()) {
                    doCancel = true;
                }
            }

            if (doConfirm && !doCancel) {
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

}

/* ========================================================================
 * Reset state when lobby session ends (called externally if needed)
 * ======================================================================== */

extern "C" void pdguiRoomScreenSetSolo(s32 solo)
{
    s_IsSoloMode = (solo != 0);
}

/* Adopt a match configuration that was initialized outside the Room renderer.
 *
 * The CLI/direct-start path and other future orchestration seams can prepare
 * g_MatchConfig before this screen gets a first frame. On Play Again,
 * pdguiSoloRoomReturn calls this boundary so the Room owns that existing
 * configuration and only rebuilds its derived catalog selectors. Calling
 * matchConfigInit here would destroy the user's arena, roster, weapons,
 * options, and limits; this function deliberately never does so. */
extern "C" void pdguiRoomScreenAdoptMatchConfig(void)
{
    if (!s_ArenasBuilt) {
        buildArenaListFromCatalog();
    }
    if (s_NumSpawnWeapons == 0 || s_SpawnWeaponCatalogGeneration != assetCatalogGetGeneration()) {
        buildSpawnWeaponList();
    }

    syncArenaFromConfig();
    syncSpawnWeaponFromConfig();
    s_MatchConfigInited = true;

    sysLogPrintf(LOG_NOTE,
        "ROOM.CONFIG: adopted existing setup stage='%s' scenario='%s' slots=%d user_options=0x%08x",
        g_MatchConfig.stage_id, g_MatchConfig.scenario_id,
        g_MatchConfig.numSlots, matchConfigGetUserOptions());
}

extern "C" void pdguiRoomScreenReset(void)
{
    roomResetSharedVariant();
    pdguiRoomClearPlayedVariant();
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
    s_RoomFocusRegion   = ROOM_FOCUS_NONE;
    s_RoomCsFocusedSection = ROOM_CS_ARENA;
    s_RoomCsSectionJumpPending = 0;
    s_RoomCsFocusTarget = -1;
    s_RoomPlayerSectionJumpPending = 0;
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
