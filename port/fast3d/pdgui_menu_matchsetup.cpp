/**
 * pdgui_menu_matchsetup.cpp -- ImGui match setup / lobby screen.
 *
 * Unified match configuration screen for both local play and network lobby.
 * The party leader (or local player in offline mode) configures:
 *   - Character slots (players + bots, up to 32 total: 8 players + up to 31 bots)
 *   - Match settings (scenario, stage, options, weapons)
 *   - Team assignments
 *
 * Replaces the old Combat Simulator menu dialog stack entirely.
 * Uses matchStart() from matchsetup.c to launch matches cleanly.
 *
 * IMPORTANT: C++ file — must NOT include types.h (#define bool s32 breaks C++).
 *
 * Auto-discovered by GLOB_RECURSE for port/*.cpp in CMakeLists.txt.
 */

#include <SDL.h>
#include <PR/ultratypes.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "imgui/imgui.h"
#include "pdgui_hotswap.h"
#include "pdgui_style.h"
#include "pdgui_scaling.h"
#include "pdgui_audio.h"
#include "pdgui_charpreview.h"
#include "assetcatalog.h"
#include "botvariant.h"
#include "screenmfst.h"
#include "net/netmanifest.h"
#include "system.h"

/* ========================================================================
 * Forward declarations (C boundary)
 * ======================================================================== */

extern "C" {

/* Dialog we replace */
extern struct menudialogdef g_MatchSetupMenuDialog;

/* Menu stack */
void menuPushDialog(struct menudialogdef *dialogdef);
void menuPopDialog(void);

/* Video */
s32 viGetWidth(void);
s32 viGetHeight(void);

/* Agent name */
const char *mpPlayerConfigGetName(s32 playernum);
u8 mpPlayerConfigGetHead(s32 playernum);
u8 mpPlayerConfigGetBody(s32 playernum);

/* Character data */
char *mpGetBodyName(u8 mpbodynum);
u32 mpGetNumBodies(void);

/* Language strings */
const char *langSafe(s32 textid);

/* Match config (from matchsetup.c — must match layout exactly).
 * Cannot include constants.h here (types.h bool conflict with C++). */
#define MAX_PLAYER_NAME 32
#define MAX_PLAYERS     8   /* = MAX_PLAYERS in src/include/constants.h */
#define MAX_BOTS        32  /* = MAX_BOTS = PARTICIPANT_DEFAULT_CAPACITY in src/include/constants.h */

#define SLOT_EMPTY    0
#define SLOT_PLAYER   1
#define SLOT_BOT      2

/* Total slots = participant pool capacity = PARTICIPANT_DEFAULT_CAPACITY (src/include/constants.h).
 * Cannot use that constant here (types.h bool conflict). Must stay in sync manually.
 * Also defined in matchsetup.c as MATCH_MAX_SLOTS = PARTICIPANT_DEFAULT_CAPACITY. */
#define MATCH_MAX_SLOTS 32  /* = PARTICIPANT_DEFAULT_CAPACITY */

struct matchslot {
    u8 type;          /* SLOT_EMPTY, SLOT_PLAYER, SLOT_BOT */
    u8 team;          /* team number (0-7) */
    /* PRIMARY: catalog IDs — always set by matchConfigInit/matchConfigAddBot.
     * bodynum/headnum are DERIVED (legacy engine indices), resolved at matchStart. */
    char body_id[64]; /* PRIMARY: catalog ID e.g. "base:dark_combat", "base:theking" */
    char head_id[64]; /* PRIMARY: catalog ID e.g. "base:head_dark_combat" */
    u8 headnum;       /* DERIVED: mpheadnum (g_MpHeads[] index) — set by matchStart */
    u8 bodynum;       /* DERIVED: mpbodynum (g_MpBodies[] index) — set by matchStart */
    u8 botType;
    u8 botDifficulty;
    char name[MAX_PLAYER_NAME];
};

struct matchconfig {
    struct matchslot slots[MATCH_MAX_SLOTS];
    u8 scenario;
    char stage_id[64];  /* PRIMARY: catalog ID e.g. "base:mp_complex" */
    u8 stagenum;        /* DERIVED: resolved from stage_id at matchStart */
    u8 timelimit;
    u8 scorelimit;
    u16 teamscorelimit;
    u32 options;
    u8 weapons[6];
    s8 weaponSetIndex;
    u8 numSlots;
    u8 spawnWeaponNum;
};

extern struct matchconfig g_MatchConfig;
void matchConfigInit(void);
s32 matchConfigAddBot(u8 botType, u8 botDifficulty, const char *body_id, const char *head_id, const char *name);
s32 matchConfigRemoveSlot(s32 idx);
s32 matchStart(void);

/* Model catalog (from modelcatalog.c) */
#define MODELSTATUS_VALID      1
#define MODELSTATUS_CLAMPED    2
s32 catalogGetNumBodies(void);
s32 catalogGetNumHeads(void);
const char *catalogGetName(s32 index);
s32 catalogGetSafeBody(s32 bodynum);
s32 catalogGetSafeHead(s32 headnum);
u32 catalogGetThumbnailTexId(s32 index);

/* Weapon set system (from mplayer.c) */
void mpSetWeaponSet(s32 weaponsetnum);
s32 mpGetWeaponSet(void);
char *mpGetWeaponSetName(s32 index);
s32 func0f189058(s32 full);     /* returns count of available weapon sets (full=1 includes Random/Custom) */
                                /* NOTE: C side uses #define bool s32, so param is s32 not C++ bool */

/* Per-slot weapon editing (Custom weapon set) */
void mpSetWeaponSlot(s32 slot, s32 mpweaponnum);
s32 mpGetWeaponSlot(s32 slot);
char *mpGetWeaponLabel(s32 weaponnum);
s32 mpGetNumWeaponOptions(void);
extern s32 g_MpWeaponSetNum;

#define WEAPONSET_CUSTOM     0x0e
#define NUM_MPWEAPONSLOTS    6

/* Arena / stage system (from modmgr + setup.c) */
struct mparena {
    s16 stagenum;
    u8 requirefeature;
    u16 name;
};
s32 modmgrGetTotalArenas(void);
struct mparena *modmgrGetArena(s32 index);
s32 challengeIsFeatureUnlocked(s32 featurenum); /* returns s32 (C bool) */

/* MP option flags (from constants.h — can't include directly) */
#define MPOPTION_ONEHITKILLS        0x00000001
#define MPOPTION_TEAMSENABLED       0x00000002
#define MPOPTION_NORADAR            0x00000004
#define MPOPTION_NOAUTOAIM          0x00000008
#define MPOPTION_SLOWMOTION_ON      0x00000040
#define MPOPTION_SLOWMOTION_SMART   0x00000080
#define MPOPTION_FASTMOVEMENT       0x00000100
#define MPOPTION_DISPLAYTEAM        0x00000200
#define MPOPTION_KILLSSCORE         0x00000400
#define MPOPTION_FRIENDLYFIRE       0x02000000

/* Scenario constants */
#define MPSCENARIO_COMBAT           0
#define MPSCENARIO_HOLDTHEBRIEFCASE 1
#define MPSCENARIO_HACKERCENTRAL    2
#define MPSCENARIO_POPACAP          3
#define MPSCENARIO_KINGOFTHEHILL    4
#define MPSCENARIO_CAPTURETHECASE   5

/* Bot types */
#define BOTTYPE_GENERAL   0
#define BOTTYPE_PEACE     1
#define BOTTYPE_SHIELD    2
#define BOTTYPE_ROCKET    3
#define BOTTYPE_KAZE      4
#define BOTTYPE_FIST      5
#define BOTTYPE_PREY      6
#define BOTTYPE_COWARD    7
#define BOTTYPE_JUDGE     8
#define BOTTYPE_FEUD      9
#define BOTTYPE_SPEED    10
#define BOTTYPE_TURTLE   11
#define BOTTYPE_VENGE    12

/* Bot difficulties */
#define BOTDIFF_MEAT      0
#define BOTDIFF_EASY      1
#define BOTDIFF_NORMAL    2
#define BOTDIFF_HARD      3
#define BOTDIFF_PERFECT   4
#define BOTDIFF_DARK      5

/* Body defaults */
#define BODY_DARK_COMBAT  0

/* Stage constants for the arena dropdown */
struct arenainfo {
    u8 stagenum;
    const char *name;
};

/* We'll populate arena list at init from game data */
s32 matchSetupGetArenaCount(void);
s32 matchSetupGetArenaInfo(s32 idx, u8 *stagenum, const char **name);

} /* extern "C" */

/* ========================================================================
 * String tables (local — not depending on game language system)
 * ======================================================================== */

static const char *s_ScenarioNames[] = {
    "Combat",
    "Hold the Briefcase",
    "Hacker Central",
    "Pop a Cap",
    "King of the Hill",
    "Capture the Case",
};
static const s32 s_NumScenarios = 6;

static const char *s_BotTypeNames[] = {
    "Normal",
    "Peace",
    "Shield",
    "Rocket",
    "Kaze",
    "Fist",
    "Prey",
    "Coward",
    "Judge",
    "Feud",
    "Speed",
    "Turtle",
    "Venge",
};
static const s32 s_NumBotTypes = 13;

static const char *s_BotDiffNames[] = {
    "Meat",
    "Easy",
    "Normal",
    "Hard",
    "Perfect",
    "Dark",
};
static const s32 s_NumBotDiffs = 6;

/* Team colors for display */
static const ImVec4 s_TeamColors[] = {
    ImVec4(0.8f, 0.2f, 0.2f, 1.0f),   /* Team 0: Red */
    ImVec4(0.2f, 0.5f, 1.0f, 1.0f),   /* Team 1: Blue */
    ImVec4(1.0f, 0.85f, 0.1f, 1.0f),  /* Team 2: Yellow */
    ImVec4(0.2f, 0.8f, 0.2f, 1.0f),   /* Team 3: Green */
    ImVec4(0.8f, 0.4f, 0.1f, 1.0f),   /* Team 4: Orange */
    ImVec4(0.7f, 0.2f, 0.8f, 1.0f),   /* Team 5: Purple */
    ImVec4(0.1f, 0.8f, 0.8f, 1.0f),   /* Team 6: Cyan */
    ImVec4(0.9f, 0.5f, 0.7f, 1.0f),   /* Team 7: Pink */
};

/* ========================================================================
 * Arena name fallback table
 *
 * The allinone mod ships its own LmpmenuE language file that overrides the
 * compiled binary at runtime.  The mod's version still contains the original
 * PerfectHead / Game Boy Camera UI strings for IDs 296-338, which means
 * langGet() returns garbage like "Load A Saved Head" instead of "Frigate".
 * This table provides the correct names keyed by text-ID so the ImGui UI
 * always shows readable arena names regardless of the language file state.
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
 * Non-static: also used by pdgui_menu_room.cpp's catalogArenaCollect(). */
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
 * Arena group cache (catalog-backed, replaces hardcoded offset table)
 *
 * Arena entries are registered in the Asset Catalog with their group name
 * as the category field.  The cache is built once from catalog iteration
 * and rebuilt when the catalog changes (mod toggle, etc.).
 *
 * The ImGui arena dropdown renders collapsible sections per group.
 * ======================================================================== */

#define ARENA_NUM_GROUPS 6
#define ARENA_MODS_GROUP (ARENA_NUM_GROUPS - 1)  /* catch-all index for mod arenas */
#define MAX_ARENAS_PER_GROUP 32

/* Group names in display order.
 * First (ARENA_NUM_GROUPS-1) must match categories in assetcatalog_base.c.
 * Last entry ("Mods") is a catch-all for any mod-registered arena whose
 * category does not match a named base-game group. */
static const char *s_ArenaGroupNames[ARENA_NUM_GROUPS] = {
    "Dark",
    "Solo Missions",
    "Classic",
    "Bonus",
    "Random",
    "Mods",
};

/* Per-group cache of catalog entry pointers */
static struct {
    const asset_entry_t *entries[MAX_ARENAS_PER_GROUP];
    s32 count;
} s_ArenaGroupCache[ARENA_NUM_GROUPS];

static s32 s_ArenaCacheDirty = 1;

/* Collapsed state: bit N = group N is collapsed */
static u8 s_ArenaGroupCollapsed = 0;

/* Callback: bucket each ASSET_ARENA entry into the right group by category.
 * Named base-game groups are checked first (indices 0..ARENA_MODS_GROUP-1).
 * Any entry whose category does not match a named group lands in the "Mods"
 * catch-all bucket (index ARENA_MODS_GROUP), so mod-registered arenas are
 * always visible in the dropdown. */
static void arenaCollectCb(const asset_entry_t *entry, void *userdata)
{
    (void)userdata;
    s32 g;
    for (g = 0; g < ARENA_MODS_GROUP; g++) {
        if (strcmp(entry->category, s_ArenaGroupNames[g]) == 0) {
            if (s_ArenaGroupCache[g].count < MAX_ARENAS_PER_GROUP) {
                s_ArenaGroupCache[g].entries[s_ArenaGroupCache[g].count++] = entry;
            }
            return;
        }
    }
    /* No named group matched — mod arena or unknown category */
    if (s_ArenaGroupCache[ARENA_MODS_GROUP].count < MAX_ARENAS_PER_GROUP) {
        s_ArenaGroupCache[ARENA_MODS_GROUP].entries[s_ArenaGroupCache[ARENA_MODS_GROUP].count++] = entry;
    }
}

/* Rebuild the group cache from the catalog.  Called lazily on first use
 * or when s_ArenaCacheDirty is set (e.g. after mod toggle). */
static void rebuildArenaCache(void)
{
    for (s32 g = 0; g < ARENA_NUM_GROUPS; g++) {
        s_ArenaGroupCache[g].count = 0;
    }
    assetCatalogIterateByType(ASSET_ARENA, arenaCollectCb, NULL);
    s_ArenaCacheDirty = 0;

    /* Diagnostic: dump arena cache contents to log */
    sysLogPrintf(LOG_NOTE, "Arena cache rebuilt:");
    for (s32 g = 0; g < ARENA_NUM_GROUPS; g++) {
        sysLogPrintf(LOG_NOTE, "  Group %d [%s]: %d arenas",
            g, s_ArenaGroupNames[g], s_ArenaGroupCache[g].count);
        for (s32 a = 0; a < s_ArenaGroupCache[g].count; a++) {
            const asset_entry_t *ae = s_ArenaGroupCache[g].entries[a];
            const char *name = arenaGetName((u16)ae->ext.arena.name_langid);
            sysLogPrintf(LOG_NOTE, "    [%d] ri=%d stagenum=0x%02x langid=0x%04x name=\"%s\" id=\"%s\"",
                a, ae->runtime_index, ae->ext.arena.stagenum,
                ae->ext.arena.name_langid, name ? name : "(null)", ae->id);
        }
    }
}

/* ========================================================================
 * Arena bridge functions (C callable)
 * ======================================================================== */

extern "C" s32 matchSetupGetArenaCount(void)
{
    return modmgrGetTotalArenas();
}

extern "C" s32 matchSetupGetArenaInfo(s32 idx, u8 *stagenum, const char **name)
{
    s32 total = modmgrGetTotalArenas();
    if (idx < 0 || idx >= total) return 0;
    struct mparena *arena = modmgrGetArena(idx);
    if (stagenum) *stagenum = (u8)arena->stagenum;
    if (name) *name = arenaGetName(arena->name);
    return 1;
}

/* ========================================================================
 * Local state
 * ======================================================================== */

static bool s_Registered = false;
static bool s_Initialized = false;
static char s_ArenaId[CATALOG_ID_LEN] = {0};  /* catalog ID of selected arena */
static bool s_NeedsFocus = false;
static s32 s_Tab = 0;                /* 0=Slots, 1=Settings */

/* Multi-selection state */
static bool s_Selected[MATCH_MAX_SLOTS] = {false};
static s32 s_SelectionCount = 0;
static s32 s_LastClickedSlot = -1;   /* For shift-click range selection */
static s32 s_PrimarySlot = -1;       /* Last-clicked slot (for detail display) */

/* Temp state for "Add Bot" popup */
static s32 s_AddBotType = BOTTYPE_GENERAL;
static s32 s_AddBotDiff = BOTDIFF_NORMAL;
static char s_AddBotName[MAX_PLAYER_NAME] = {0};

/* Arena picker modal state */
static bool s_ArenaModalOpen = false;
static char s_ArenaHoverId[CATALOG_ID_LEN] = {0};  /* catalog ID of hovered arena for preview */

/* ========================================================================
 * Selection helpers
 * ======================================================================== */

static void selectionClear(void)
{
    memset(s_Selected, 0, sizeof(s_Selected));
    s_SelectionCount = 0;
    s_PrimarySlot = -1;
    s_LastClickedSlot = -1;
}

static void selectionSet(s32 idx)
{
    selectionClear();
    if (idx >= 0 && idx < MATCH_MAX_SLOTS) {
        s_Selected[idx] = true;
        s_SelectionCount = 1;
        s_PrimarySlot = idx;
        s_LastClickedSlot = idx;
    }
}

/* selectionToggle, selectionRange, selectionHasBots, selectionAllBots
 * removed — were used by the old renderSlotDetail multi-select panel,
 * now replaced by per-bot popup editing in renderPlayersPanel. */


/* ========================================================================
 * Sound-playing widget wrappers (matching main menu style)
 * ======================================================================== */

extern "C" void pdguiDrawButtonEdgeGlow(f32 x, f32 y, f32 w, f32 h, s32 isActive);

static bool PdButton(const char *label, const ImVec2 &size = ImVec2(0,0))
{
    bool clicked = ImGui::Button(label, size);
    if (clicked) pdguiPlaySound(PDGUI_SND_SELECT);
    /* Draw edge glow for hover (mouse), active (pressed), AND focused (gamepad nav) */
    if (ImGui::IsItemHovered() || ImGui::IsItemActive() || ImGui::IsItemFocused()) {
        ImVec2 rmin = ImGui::GetItemRectMin();
        ImVec2 rmax = ImGui::GetItemRectMax();
        pdguiDrawButtonEdgeGlow(rmin.x, rmin.y,
                                rmax.x - rmin.x, rmax.y - rmin.y,
                                ImGui::IsItemActive() ? 1 : 0);
    }
    return clicked;
}

static bool PdCheckbox(const char *label, bool *v)
{
    bool changed = ImGui::Checkbox(label, v);
    if (changed) pdguiPlaySound(*v ? PDGUI_SND_TOGGLEON : PDGUI_SND_TOGGLEOFF);
    return changed;
}

/* ========================================================================
 * Render: Players panel (right side — per Mike's layout)
 *
 * Shows player/bot list, click a bot to open edit popup, +/- bot buttons
 * docked to bottom.
 * ======================================================================== */

/* State for the bot edit popup */
static bool  s_BotPopupOpen = false;
static s32   s_BotPopupSlot = -1;
static float s_BotPreviewRotY = 0.0f;  /* accumulated rotation for char preview */

/* ========================================================================
 * D3R-8: Bot Customizer state
 * ======================================================================== */

/* Per-slot trait overrides (parallel to g_MatchConfig.slots[], UI state only) */
struct BotTraits {
    float accuracy;
    float reactionTime;
    float aggression;
    char  baseType[32];
};

static BotTraits s_BotTraits[MATCH_MAX_SLOTS];
static bool      s_BotTraitsInitialized = false;

/* Whether the Advanced section is expanded in the current bot edit popup */
static bool s_BotPopupShowAdvanced = false;

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

/* ========================================================================
 * Bot preset cache helpers
 * ======================================================================== */

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

/* Initialize default traits for all slots (called once per session) */
static void initBotTraits(void)
{
    for (s32 i = 0; i < MATCH_MAX_SLOTS; i++) {
        s_BotTraits[i].accuracy    = 0.5f;
        s_BotTraits[i].reactionTime = 0.5f;
        s_BotTraits[i].aggression  = 0.5f;
        strncpy(s_BotTraits[i].baseType, "NormalSim", sizeof(s_BotTraits[i].baseType) - 1);
        s_BotTraits[i].baseType[sizeof(s_BotTraits[i].baseType) - 1] = '\0';
    }
    s_BotTraitsInitialized = true;
}

static void renderPlayersPanel(float scale, float panelW, float panelH)
{
    ImGui::BeginChild("##players_panel", ImVec2(panelW, panelH), true,
                      ImGuiChildFlags_NavFlattened);

    /* Header */
    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f),
                       "Players (%d/%d)", g_MatchConfig.numSlots, MATCH_MAX_SLOTS);
    ImGui::Separator();

    bool teamsOn = (g_MatchConfig.options & MPOPTION_TEAMSENABLED) != 0;

    /* Scrollable player list — reserve space at bottom for +/- buttons */
    float botBtnAreaH = 40.0f * scale;
    float listH = panelH - botBtnAreaH - ImGui::GetCursorPosY()
                  - ImGui::GetStyle().WindowPadding.y;

    ImGui::BeginChild("##player_scroll", ImVec2(0, listH), false);

    for (s32 i = 0; i < g_MatchConfig.numSlots; i++) {
        struct matchslot *slot = &g_MatchConfig.slots[i];
        ImGui::PushID(i);

        /* Type tag */
        const char *typeStr = (slot->type == SLOT_PLAYER) ? "[P]" : "[B]";
        ImVec4 typeColor = (slot->type == SLOT_PLAYER)
            ? ImVec4(0.3f, 1.0f, 0.3f, 1.0f)
            : ImVec4(1.0f, 0.7f, 0.2f, 1.0f);

        /* Team color dot */
        if (teamsOn) {
            s32 team = slot->team < 8 ? slot->team : 0;
            ImGui::TextColored(s_TeamColors[team], "\xe2\x97\x8f");
            ImGui::SameLine();
        }

        ImGui::TextColored(typeColor, "%s", typeStr);
        ImGui::SameLine();

        /* Name + difficulty for bots */
        char label[96];
        if (slot->type == SLOT_BOT) {
            const char *diffStr = (slot->botDifficulty < s_NumBotDiffs)
                ? s_BotDiffNames[slot->botDifficulty] : "?";
            snprintf(label, sizeof(label), "%s (%sSim)##slot%d", slot->name, diffStr, i);
        } else {
            snprintf(label, sizeof(label), "%s##slot%d", slot->name, i);
        }

        /* Click selects; double-click (or single click on bot) opens edit popup */
        bool isSelected = s_Selected[i];
        if (ImGui::Selectable(label, isSelected,
                              ImGuiSelectableFlags_AllowDoubleClick,
                              ImVec2(0, 0)))
        {
            selectionSet(i);
            pdguiPlaySound(PDGUI_SND_SUBFOCUS);

            /* Open bot edit popup on click */
            if (slot->type == SLOT_BOT) {
                s_BotPopupOpen = true;
                s_BotPopupSlot = i;
                ImGui::OpenPopup("##bot_edit_popup");
            }
        }

        /* Right-click context or remove button for bots */
        if (i > 0 && slot->type == SLOT_BOT) {
            ImGui::SameLine(panelW - 32.0f * scale);
            if (ImGui::SmallButton("X")) {
                pdguiPlaySound(PDGUI_SND_KBCANCEL);
                s_Selected[i] = false;
                matchConfigRemoveSlot(i);
                /* Shift selection indices */
                for (s32 j = i; j < MATCH_MAX_SLOTS - 1; j++) {
                    s_Selected[j] = s_Selected[j + 1];
                }
                s_Selected[MATCH_MAX_SLOTS - 1] = false;
                s_SelectionCount = 0;
                s_PrimarySlot = -1;
                for (s32 j = 0; j < MATCH_MAX_SLOTS; j++) {
                    if (s_Selected[j]) {
                        s_SelectionCount++;
                        if (s_PrimarySlot < 0) s_PrimarySlot = j;
                    }
                }
                ImGui::PopID();
                break;
            }
        }

        ImGui::PopID();
    }

    /* ---- Bot edit popup ---- */
    if (ImGui::BeginPopup("##bot_edit_popup")) {
        if (s_BotPopupSlot >= 0 && s_BotPopupSlot < g_MatchConfig.numSlots
            && g_MatchConfig.slots[s_BotPopupSlot].type == SLOT_BOT)
        {
            struct matchslot *bot = &g_MatchConfig.slots[s_BotPopupSlot];

            /* Two-column layout: left = controls, right = 3D character preview */
            float previewSz = pdguiScale(160.0f);
            float ctrlW     = pdguiScale(260.0f);

            ImGui::BeginGroup();  /* --- Left: controls --- */

            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "Edit Bot");
            ImGui::Separator();
            ImGui::Spacing();

            /* Name */
            ImGui::SetNextItemWidth(ctrlW);
            ImGui::InputText("Name", bot->name, sizeof(bot->name));

            /* Character body — scrollable list (no combo, full list visible) */
            {
                u32 numBodies = mpGetNumBodies();

                ImGui::Text("Character:");
                ImGui::BeginChild("##char_list", ImVec2(ctrlW, pdguiScale(110.0f)),
                                  true, ImGuiWindowFlags_AlwaysVerticalScrollbar);
                for (u32 b = 0; b < numBodies && b < 200; b++) {
                    const char *bName = mpGetBodyName((u8)b);
                    if (!bName || !bName[0]) continue;
                    const char *bid = catalogResolveBodyByMpIndex((s32)b);
                    bool isSel = bid && bot->body_id[0] && strcmp(bid, bot->body_id) == 0;
                    char itemLabel[64];
                    snprintf(itemLabel, sizeof(itemLabel), "%s##body%d", bName, b);
                    if (ImGui::Selectable(itemLabel, isSel)) {
                        if (bid) {
                            strncpy(bot->body_id, bid, sizeof(bot->body_id) - 1);
                            bot->body_id[sizeof(bot->body_id) - 1] = '\0';
                        }
                        const char *hid = catalogResolveHeadByMpIndex((s32)b);
                        if (hid) {
                            strncpy(bot->head_id, hid, sizeof(bot->head_id) - 1);
                            bot->head_id[sizeof(bot->head_id) - 1] = '\0';
                        }
                        s_BotPreviewRotY = 0.0f;
                        pdguiPlaySound(PDGUI_SND_SUBFOCUS);
                    }
                    if (isSel) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndChild();
            }

            /* Bot type */
            {
                int botType = bot->botType;
                if (ImGui::Combo("Type", &botType, s_BotTypeNames, s_NumBotTypes)) {
                    bot->botType = (u8)botType;
                    pdguiPlaySound(PDGUI_SND_SUBFOCUS);
                }
            }

            /* Bot difficulty */
            {
                int botDiff = bot->botDifficulty;
                if (ImGui::Combo("Difficulty", &botDiff, s_BotDiffNames, s_NumBotDiffs)) {
                    bot->botDifficulty = (u8)botDiff;
                    pdguiPlaySound(PDGUI_SND_SUBFOCUS);
                }
            }

            /* Team (if teams enabled) */
            if (g_MatchConfig.options & MPOPTION_TEAMSENABLED) {
                static const char *teamNames[] = {
                    "Red", "Blue", "Yellow", "Green",
                    "Orange", "Purple", "Cyan", "Pink"
                };
                int team = bot->team;
                if (team >= 8) team = 0;
                if (ImGui::Combo("Team", &team, teamNames, 8)) {
                    bot->team = (u8)team;
                    pdguiPlaySound(PDGUI_SND_SUBFOCUS);
                }
            }

            /* ---- Advanced / Simple toggle ---- */
            ImGui::Spacing();
            ImGui::Separator();

            const char *advLabel = s_BotPopupShowAdvanced ? "- Simple -" : "+ Advanced";
            if (PdButton(advLabel, ImVec2(120.0f * scale, 0))) {
                s_BotPopupShowAdvanced = !s_BotPopupShowAdvanced;
                if (s_BotPopupShowAdvanced && s_BotPresetCacheDirty) {
                    rebuildBotPresetCache();
                    s_BotPresetSelected = -1;
                }
            }

            /* ---- Advanced section ---- */
            if (s_BotPopupShowAdvanced && s_BotPopupSlot >= 0
                && s_BotPopupSlot < MATCH_MAX_SLOTS)
            {
                BotTraits *traits = &s_BotTraits[s_BotPopupSlot];

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
                                /* Apply preset values to trait editor */
                                traits->accuracy    = preset->ext.bot_variant.accuracy;
                                traits->reactionTime = preset->ext.bot_variant.reaction_time;
                                traits->aggression  = preset->ext.bot_variant.aggression;
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

                /* Base Type */
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
                ImGui::SliderFloat("Accuracy",  &traits->accuracy,    0.0f, 1.0f, "%.2f");
                ImGui::SliderFloat("Reaction",  &traits->reactionTime, 0.0f, 1.0f, "%.2f");
                ImGui::SliderFloat("Aggression",&traits->aggression,   0.0f, 1.0f, "%.2f");

                ImGui::Spacing();

                /* Save as Preset button */
                if (PdButton("Save as Preset...", ImVec2(160.0f * scale, 0))) {
                    s_SavePresetName[0] = '\0';
                    ImGui::OpenPopup("##save_preset");
                }

                /* Save preset nested popup */
                if (ImGui::BeginPopup("##save_preset")) {
                    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "Save Bot Preset");
                    ImGui::Separator();
                    ImGui::Spacing();
                    ImGui::SetNextItemWidth(200.0f * scale);
                    ImGui::InputText("Name##psname", s_SavePresetName,
                                     sizeof(s_SavePresetName));
                    ImGui::Spacing();

                    bool canSave = (s_SavePresetName[0] != '\0');
                    if (!canSave) ImGui::BeginDisabled();
                    if (PdButton("Save##pssave", ImVec2(80.0f * scale, 0))) {
                        if (botVariantSave(s_SavePresetName,
                                           traits->baseType,
                                           traits->accuracy,
                                           traits->reactionTime,
                                           traits->aggression,
                                           "custom", "", "")) {
                            /* Dirty cache so next open shows the new preset */
                            s_BotPresetCacheDirty = 1;
                        }
                        ImGui::CloseCurrentPopup();
                    }
                    if (!canSave) ImGui::EndDisabled();

                    ImGui::SameLine();
                    if (PdButton("Cancel##pscancel", ImVec2(80.0f * scale, 0))) {
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndPopup();
                }
            } /* end Advanced section */

            ImGui::Spacing();
            if (PdButton("Done", ImVec2(80.0f * scale, 0))) {
                s_BotPopupShowAdvanced = false;
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndGroup();  /* end left column */

            /* ---- Right column: 3D character preview ---- */
            ImGui::SameLine(0, pdguiScale(12.0f));
            ImGui::BeginGroup();

            /* Request preview render for current body+head */
            s_BotPreviewRotY += 0.022f;  /* ~1.26 rad/s at 60fps */
            if (s_BotPreviewRotY > 6.2832f) s_BotPreviewRotY -= 6.2832f;
            pdguiCharPreviewSetRotY(s_BotPreviewRotY);
            pdguiCharPreviewRequest(bot->headnum, bot->bodynum);

            float previewSzR = pdguiScale(160.0f);
            s32 prevW = 1, prevH = 1;
            pdguiCharPreviewGetSize(&prevW, &prevH);

            if (pdguiCharPreviewIsReady()) {
                ImTextureID texId = (ImTextureID)(uintptr_t)pdguiCharPreviewGetTextureId();
                ImGui::Image(texId, ImVec2(previewSzR, previewSzR));
            } else {
                /* Placeholder while first frame renders */
                ImVec2 cursor = ImGui::GetCursorScreenPos();
                ImGui::GetWindowDrawList()->AddRectFilled(
                    cursor,
                    ImVec2(cursor.x + previewSzR, cursor.y + previewSzR),
                    IM_COL32(20, 20, 30, 200));
                ImGui::Dummy(ImVec2(previewSzR, previewSzR));
            }

            /* Character name label below preview */
            {
                const char *bName = mpGetBodyName(bot->bodynum);
                if (bName && bName[0]) {
                    float textW = ImGui::CalcTextSize(bName).x;
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX()
                                         + (previewSzR - textW) * 0.5f);
                    ImGui::TextColored(ImVec4(0.4f, 0.9f, 1.0f, 1.0f), "%s", bName);
                }
            }

            ImGui::EndGroup();  /* end right column */
        }
        ImGui::EndPopup();
    }

    ImGui::EndChild(); /* player_scroll */

    /* ---- Add Bot button docked to bottom ---- */
    ImGui::Spacing();

    /* Count current bots */
    s32 botCount = 0;
    for (s32 i = 0; i < g_MatchConfig.numSlots; i++) {
        if (g_MatchConfig.slots[i].type == SLOT_BOT) botCount++;
    }

    float addBtnW = 100.0f * scale;
    float addBtnH = 26.0f * scale;

    /* Right-align: bot count label + Add Bot button */
    float botLabelW = ImGui::CalcTextSize("Bots: 00").x;
    float btnPad = 8.0f * scale;
    float totalW = botLabelW + btnPad + addBtnW;
    ImGui::SetCursorPosX(panelW - totalW - ImGui::GetStyle().WindowPadding.x);

    ImGui::AlignTextToFramePadding();
    ImGui::Text("Bots: %02d", botCount);
    ImGui::SameLine(0, btnPad);

    /* Add Bot button */
    bool canAdd = (g_MatchConfig.numSlots < MATCH_MAX_SLOTS);
    if (!canAdd) ImGui::BeginDisabled();
    if (PdButton("Add Bot", ImVec2(addBtnW, addBtnH))) {
        s32 newIdx = matchConfigAddBot(
            (u8)s_AddBotType, (u8)s_AddBotDiff,
            "base:dark_combat", "base:head_dark_combat", NULL);
        if (newIdx >= 0) selectionSet(newIdx);
    }
    if (!canAdd) ImGui::EndDisabled();

    ImGui::EndChild(); /* players_panel */
}

/* renderSlotDetail removed — bot editing now handled by popup in renderPlayersPanel */

/* ========================================================================
 * Render: Match settings panel
 * ======================================================================== */

static void renderMatchSettings(float scale, float panelW, float panelH)
{
    ImGui::BeginChild("##match_settings", ImVec2(panelW, panelH), true,
                      ImGuiChildFlags_NavFlattened);

    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Match Settings");
    ImGui::Separator();

    /* Scenario */
    {
        int scenario = g_MatchConfig.scenario;
        if (scenario >= s_NumScenarios) scenario = 0;
        if (ImGui::Combo("Scenario", &scenario, s_ScenarioNames, s_NumScenarios)) {
            g_MatchConfig.scenario = (u8)scenario;
            pdguiPlaySound(PDGUI_SND_SUBFOCUS);
        }
    }

    /* Arena/Stage — button opens a full-screen submenu modal */
    {
        if (s_ArenaCacheDirty) {
            rebuildArenaCache();
        }

        /* Resolve current arena name for the button label */
        const char *curArenaName = "???";
        const char *curArenaGroup = "";
        for (s32 g = 0; g < ARENA_NUM_GROUPS; g++) {
            for (s32 a = 0; a < s_ArenaGroupCache[g].count; a++) {
                if (s_ArenaId[0] && strcmp(s_ArenaGroupCache[g].entries[a]->id, s_ArenaId) == 0) {
                    curArenaName  = arenaGetName((u16)s_ArenaGroupCache[g].entries[a]->ext.arena.name_langid);
                    curArenaGroup = s_ArenaGroupNames[g];
                    goto found_arena_label;
                }
            }
        }
        found_arena_label:

        /* Row: "Arena:" label + button showing current selection */
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(ImVec4(0.65f, 0.65f, 0.65f, 1.0f), "Arena");
        ImGui::SameLine();
        char arenaBtnLabel[96];
        snprintf(arenaBtnLabel, sizeof(arenaBtnLabel), "  %s  [%s]##arena_btn",
                 curArenaName ? curArenaName : "???",
                 curArenaGroup[0] ? curArenaGroup : "?");
        float arenaBtnW = panelW - ImGui::GetCursorPosX()
                          - ImGui::GetStyle().WindowPadding.x;
        if (PdButton(arenaBtnLabel, ImVec2(arenaBtnW, 0))) {
            s_ArenaModalOpen = true;
            strncpy(s_ArenaHoverId, s_ArenaId, CATALOG_ID_LEN);
            ImGui::OpenPopup("##arena_modal");
        }

        /* ---- Arena picker modal ---- */
        ImVec2 center = ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f,
                               ImGui::GetIO().DisplaySize.y * 0.5f);
        ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        float modalW = pdguiMenuWidth() * 0.85f;
        float modalH = pdguiMenuHeight() * 0.85f;
        ImGui::SetNextWindowSize(ImVec2(modalW, modalH), ImGuiCond_Always);

        if (ImGui::BeginPopup("##arena_modal",
                              ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
                              | ImGuiWindowFlags_NoTitleBar))
        {
            /* Backdrop */
            ImVec2 mpos = ImGui::GetWindowPos();
            ImGui::GetWindowDrawList()->AddRectFilled(
                mpos, ImVec2(mpos.x + modalW, mpos.y + modalH),
                IM_COL32(8, 8, 16, 250));

            pdguiDrawPdDialog(mpos.x, mpos.y, modalW, modalH, "Select Arena", 1);

            /* Title */
            {
                const char *title = "Select Arena";
                float titleH = pdguiScale(26.0f);
                ImDrawList *dl = ImGui::GetWindowDrawList();
                pdguiDrawTextGlow(mpos.x + 8.0f, mpos.y + 2.0f,
                                  modalW - 16.0f, titleH - 4.0f);
                ImVec2 ts = ImGui::CalcTextSize(title);
                dl->AddText(ImVec2(mpos.x + (modalW - ts.x) * 0.5f,
                                   mpos.y + (titleH - ts.y) * 0.5f),
                            IM_COL32(255, 255, 255, 255), title);
                ImGui::SetCursorPosY(titleH + ImGui::GetStyle().WindowPadding.y);
            }

            float footerH = pdguiScale(42.0f);
            float contentH = modalH - pdguiScale(26.0f) - footerH
                             - ImGui::GetStyle().WindowPadding.y * 2.0f;
            float listW  = modalW * 0.52f;
            float detailW = modalW - listW - pdguiScale(8.0f);

            /* ---- Left: grouped arena list ---- */
            ImGui::BeginChild("##arena_list", ImVec2(listW, contentH), true,
                              ImGuiWindowFlags_AlwaysVerticalScrollbar);

            for (s32 g = 0; g < ARENA_NUM_GROUPS; g++) {
                s32 groupCount = 0;
                for (s32 a = 0; a < s_ArenaGroupCache[g].count; a++) {
                    if (challengeIsFeatureUnlocked(s_ArenaGroupCache[g].entries[a]->ext.arena.requirefeature))
                        groupCount++;
                }
                if (groupCount == 0) continue;

                /* Group header — always expanded in modal, no toggle needed */
                ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f),
                                   "%s", s_ArenaGroupNames[g]);
                ImGui::Separator();

                for (s32 a = 0; a < s_ArenaGroupCache[g].count; a++) {
                    const asset_entry_t *ae = s_ArenaGroupCache[g].entries[a];
                    if (!challengeIsFeatureUnlocked(ae->ext.arena.requirefeature)) continue;

                    const char *arenaName = arenaGetName((u16)ae->ext.arena.name_langid);
                    if (!arenaName || !arenaName[0]) continue;

                    bool isSel = (ae->id[0] && s_ArenaId[0] && strcmp(ae->id, s_ArenaId) == 0);
                    bool isHov = (ae->id[0] && s_ArenaHoverId[0] && strcmp(ae->id, s_ArenaHoverId) == 0);
                    char arenaLabel[96];
                    snprintf(arenaLabel, sizeof(arenaLabel), "  %s##arena_%d_%d",
                             arenaName, g, a);

                    if (ImGui::Selectable(arenaLabel, isSel || isHov,
                                          ImGuiSelectableFlags_None)) {
                        strncpy(s_ArenaId, ae->id, CATALOG_ID_LEN - 1);
                        s_ArenaId[CATALOG_ID_LEN - 1] = '\0';
                        strncpy(s_ArenaHoverId, ae->id, CATALOG_ID_LEN - 1);
                        s_ArenaHoverId[CATALOG_ID_LEN - 1] = '\0';
                        strncpy(g_MatchConfig.stage_id, ae->id, sizeof(g_MatchConfig.stage_id) - 1);
                        g_MatchConfig.stage_id[sizeof(g_MatchConfig.stage_id) - 1] = '\0';
                        sysLogPrintf(LOG_NOTE, "Arena: \"%s\" id='%s'", arenaName, ae->id);
                        pdguiPlaySound(PDGUI_SND_SUBFOCUS);
                        ImGui::CloseCurrentPopup();
                    }
                    if (ImGui::IsItemHovered()) {
                        strncpy(s_ArenaHoverId, ae->id, CATALOG_ID_LEN - 1);
                        s_ArenaHoverId[CATALOG_ID_LEN - 1] = '\0';
                    }
                    if (isSel) ImGui::SetItemDefaultFocus();
                }
                ImGui::Spacing();
            }

            ImGui::EndChild();

            ImGui::SameLine(0, pdguiScale(8.0f));

            /* ---- Right: detail / preview panel ---- */
            ImGui::BeginChild("##arena_detail", ImVec2(detailW, contentH), true);

            /* Resolve hovered arena info */
            const char *hoverName  = nullptr;
            const char *hoverGroup = nullptr;
            u8 hoverStage = 0;
            for (s32 g = 0; g < ARENA_NUM_GROUPS && !hoverName; g++) {
                for (s32 a = 0; a < s_ArenaGroupCache[g].count; a++) {
                    const asset_entry_t *he = s_ArenaGroupCache[g].entries[a];
                    if (s_ArenaHoverId[0] && he->id[0] && strcmp(he->id, s_ArenaHoverId) == 0) {
                        hoverName  = arenaGetName((u16)he->ext.arena.name_langid);
                        hoverGroup = s_ArenaGroupNames[g];
                        hoverStage = (u8)he->ext.arena.stagenum;
                        break;
                    }
                }
            }

            if (hoverName && hoverName[0]) {
                /* Arena name — large */
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "%s", hoverName);
                ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 0.8f), "%s",
                                   hoverGroup ? hoverGroup : "");
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                /* Preview placeholder — stylized frame with stage number.
                 * A real map thumbnail system would render here in future. */
                float previewW = detailW - ImGui::GetStyle().WindowPadding.x * 2.0f;
                float previewH = previewW * 0.6f;   /* 5:3 aspect */
                ImVec2 p0 = ImGui::GetCursorScreenPos();
                ImVec2 p1 = ImVec2(p0.x + previewW, p0.y + previewH);

                ImDrawList *dl = ImGui::GetWindowDrawList();
                /* Dark background */
                dl->AddRectFilled(p0, p1, IM_COL32(12, 18, 30, 230), pdguiScale(4.0f));
                /* Decorative border */
                dl->AddRect(p0, p1, IM_COL32(60, 120, 180, 160),
                            pdguiScale(4.0f), 0, pdguiScale(1.5f));
                /* Stage ID badge */
                char stageBadge[16];
                snprintf(stageBadge, sizeof(stageBadge), "0x%02X", (unsigned)hoverStage);
                ImVec2 badgePos = ImVec2(p0.x + pdguiScale(6.0f),
                                         p0.y + pdguiScale(6.0f));
                dl->AddText(badgePos, IM_COL32(80, 160, 220, 180), stageBadge);
                /* Centered arena name in preview box */
                ImVec2 nameSize = ImGui::CalcTextSize(hoverName);
                ImVec2 namePos  = ImVec2(
                    p0.x + (previewW - nameSize.x) * 0.5f,
                    p0.y + (previewH - nameSize.y) * 0.5f);
                dl->AddText(namePos, IM_COL32(200, 220, 255, 200), hoverName);

                /* Diagonal scan-line effect for sci-fi feel */
                for (float fy = p0.y + pdguiScale(6.0f);
                     fy < p1.y - pdguiScale(2.0f);
                     fy += pdguiScale(8.0f))
                {
                    dl->AddLine(ImVec2(p0.x, fy), ImVec2(p1.x, fy),
                                IM_COL32(40, 80, 120, 30));
                }

                ImGui::Dummy(ImVec2(previewW, previewH));
                ImGui::Spacing();
                ImGui::TextDisabled("Stage 0x%02X", (unsigned)hoverStage);
            } else {
                ImGui::Spacing();
                ImGui::TextDisabled("Hover over an arena to preview");
            }

            ImGui::EndChild();

            /* ---- Footer ---- */
            ImGui::SetCursorPosY(modalH - footerH + pdguiScale(6.0f));
            ImGui::Separator();
            ImGui::Spacing();
            float closeBtnW = pdguiScale(120.0f);
            ImGui::SetCursorPosX((modalW - closeBtnW) * 0.5f);
            if (PdButton("Close", ImVec2(closeBtnW, pdguiScale(26.0f)))
                || ImGui::IsKeyPressed(ImGuiKey_Escape, false)
                || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false))
            {
                pdguiPlaySound(PDGUI_SND_KBCANCEL);
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }
    }

    /* Weapon Set */
    {
        s32 numSets = func0f189058(1); /* total including Random/Custom */
        s32 curSet = mpGetWeaponSet();
        char *curName = mpGetWeaponSetName(curSet);

        if (ImGui::BeginCombo("Weapons", curName ? curName : "???")) {
            for (s32 i = 0; i < numSets; i++) {
                char *setName = mpGetWeaponSetName(i);
                if (!setName || !setName[0]) continue;
                bool isSel = (i == curSet);
                char setLabel[64];
                snprintf(setLabel, sizeof(setLabel), "%s##wset%d", setName, i);
                if (ImGui::Selectable(setLabel, isSel)) {
                    mpSetWeaponSet(i);
                    g_MatchConfig.weaponSetIndex = (s8)i;
                    pdguiPlaySound(PDGUI_SND_SUBFOCUS);
                }
                if (isSel) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

    }

    /* ---- Length / Score (inline below the dropdowns, matching width) ---- */
    /* Time limit
     * Engine encoding: (value + 1) * 60 seconds.  >=60 means no limit.
     * We show the user-facing minutes: value+1 for 0-59, "No Limit" for >=60. */
    {
        int tl = g_MatchConfig.timelimit;
        char tlFmt[32];
        if (tl >= 60) {
            snprintf(tlFmt, sizeof(tlFmt), "No Limit");
        } else {
            snprintf(tlFmt, sizeof(tlFmt), "%d min", tl + 1);
        }
        if (ImGui::SliderInt("Length", &tl, 0, 60, tlFmt)) {
            g_MatchConfig.timelimit = (u8)tl;
        }
    }

    /* Score limit
     * Engine encoding: value + 1 kills.  >=100 means no limit. */
    {
        int sl = g_MatchConfig.scorelimit;
        char slFmt[32];
        if (sl >= 100) {
            snprintf(slFmt, sizeof(slFmt), "No Limit");
        } else {
            snprintf(slFmt, sizeof(slFmt), "%d kills", sl + 1);
        }
        if (ImGui::SliderInt("Score", &sl, 0, 100, slFmt)) {
            g_MatchConfig.scorelimit = (u8)sl;
        }
    }

    /* Team score limit (only if teams enabled) */
    if (g_MatchConfig.options & MPOPTION_TEAMSENABLED) {
        int tsl = g_MatchConfig.teamscorelimit;
        char tslFmt[32];
        if (tsl >= 400) {
            snprintf(tslFmt, sizeof(tslFmt), "No Limit");
        } else {
            snprintf(tslFmt, sizeof(tslFmt), "%d", tsl + 1);
        }
        if (ImGui::SliderInt("Team Score", &tsl, 0, 400, tslFmt)) {
            g_MatchConfig.teamscorelimit = (u16)tsl;
        }
    }

    /* ---- Weapon slots — always visible (Slot 1 – Slot 6) ---- */
    ImGui::Spacing();
    {
        s32 numWeaponOptions = mpGetNumWeaponOptions();
        static const char *slotLabels[NUM_MPWEAPONSLOTS] = {
            "Slot 1", "Slot 2", "Slot 3", "Slot 4", "Slot 5", "Slot 6"
        };

        /* When not Custom weapon set, slots reflect the preset but are still
         * visible (read-only feel — selecting Custom unlocks full editing). */
        bool isCustom = (g_MpWeaponSetNum == WEAPONSET_CUSTOM);

        for (s32 slot = 0; slot < NUM_MPWEAPONSLOTS; slot++) {
            s32 curWeapon = mpGetWeaponSlot(slot);
            char *curWeaponName = mpGetWeaponLabel(curWeapon);

            if (!isCustom) ImGui::BeginDisabled();
            if (ImGui::BeginCombo(slotLabels[slot],
                                  curWeaponName ? curWeaponName : "???")) {
                for (s32 w = 0; w < numWeaponOptions; w++) {
                    char *wName = mpGetWeaponLabel(w);
                    if (!wName || !wName[0]) continue;
                    bool isSel = (w == curWeapon);
                    char wLabel[64];
                    snprintf(wLabel, sizeof(wLabel), "%s##ws%d_%d", wName, slot, w);
                    if (ImGui::Selectable(wLabel, isSel)) {
                        mpSetWeaponSlot(slot, w);
                        pdguiPlaySound(PDGUI_SND_SUBFOCUS);
                    }
                    if (isSel) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            if (!isCustom) ImGui::EndDisabled();
        }
    }

    /* ---- Options (collapsible section below weapon slots) ---- */
    ImGui::Spacing();
    if (ImGui::CollapsingHeader("Options", ImGuiTreeNodeFlags_DefaultOpen)) {
        bool v;

        v = (g_MatchConfig.options & MPOPTION_TEAMSENABLED) != 0;
        if (PdCheckbox("Teams", &v)) {
            if (v) g_MatchConfig.options |= MPOPTION_TEAMSENABLED;
            else g_MatchConfig.options &= ~MPOPTION_TEAMSENABLED;
        }

        v = (g_MatchConfig.options & MPOPTION_ONEHITKILLS) != 0;
        if (PdCheckbox("One-Hit Kills", &v)) {
            if (v) g_MatchConfig.options |= MPOPTION_ONEHITKILLS;
            else g_MatchConfig.options &= ~MPOPTION_ONEHITKILLS;
        }

        v = (g_MatchConfig.options & MPOPTION_NORADAR) != 0;
        if (PdCheckbox("No Radar", &v)) {
            if (v) g_MatchConfig.options |= MPOPTION_NORADAR;
            else g_MatchConfig.options &= ~MPOPTION_NORADAR;
        }

        v = (g_MatchConfig.options & MPOPTION_NOAUTOAIM) != 0;
        if (PdCheckbox("No Auto-Aim", &v)) {
            if (v) g_MatchConfig.options |= MPOPTION_NOAUTOAIM;
            else g_MatchConfig.options &= ~MPOPTION_NOAUTOAIM;
        }

        v = (g_MatchConfig.options & MPOPTION_FASTMOVEMENT) != 0;
        if (PdCheckbox("Fast Movement", &v)) {
            if (v) g_MatchConfig.options |= MPOPTION_FASTMOVEMENT;
            else g_MatchConfig.options &= ~MPOPTION_FASTMOVEMENT;
        }

        v = (g_MatchConfig.options & MPOPTION_SLOWMOTION_ON) != 0;
        if (PdCheckbox("Slow Motion", &v)) {
            if (v) g_MatchConfig.options |= MPOPTION_SLOWMOTION_ON;
            else g_MatchConfig.options &= ~MPOPTION_SLOWMOTION_ON;
        }

        if (g_MatchConfig.options & MPOPTION_TEAMSENABLED) {
            v = (g_MatchConfig.options & MPOPTION_FRIENDLYFIRE) != 0;
            if (PdCheckbox("Friendly Fire", &v)) {
                if (v) g_MatchConfig.options |= MPOPTION_FRIENDLYFIRE;
                else g_MatchConfig.options &= ~MPOPTION_FRIENDLYFIRE;
            }
        }
    }

    ImGui::EndChild();
}

/* ========================================================================
 * Main render function — called via hotswap
 * ======================================================================== */

static s32 renderMatchSetup(struct menudialog *dialog,
                             struct menu *menu,
                             s32 winW, s32 winH)
{
    /* Initialize match config on first appearance */
    if (!s_Initialized) {
        matchConfigInit();
        strncpy(s_ArenaId, g_MatchConfig.stage_id, CATALOG_ID_LEN - 1);
        s_ArenaId[CATALOG_ID_LEN - 1] = '\0';
        selectionClear();
        s_Tab = 0;
        s_Initialized = true;
    }

    /* Initialize bot trait defaults once per session */
    if (!s_BotTraitsInitialized) {
        initBotTraits();
    }

    float scale = pdguiScaleFactor();
    float dialogW = pdguiMenuWidth();
    float dialogH = pdguiMenuHeight();
    ImVec2 menuPos = pdguiMenuPos();
    float dialogX = menuPos.x;
    float dialogY = menuPos.y;
    float pdTitleH = pdguiScale(26.0f);

    ImGuiWindowFlags wflags = ImGuiWindowFlags_NoResize
                            | ImGuiWindowFlags_NoMove
                            | ImGuiWindowFlags_NoCollapse
                            | ImGuiWindowFlags_NoSavedSettings
                            | ImGuiWindowFlags_NoTitleBar
                            | ImGuiWindowFlags_NoBackground;

    ImGui::SetNextWindowPos(ImVec2(dialogX, dialogY));
    ImGui::SetNextWindowSize(ImVec2(dialogW, dialogH));

    if (!ImGui::Begin("##match_setup", nullptr, wflags)) {
        ImGui::End();
        return 1;
    }

    if (ImGui::IsWindowAppearing()) {
        ImGui::SetWindowFocus();
        s_NeedsFocus = true;
        sysLogPrintf(LOG_NOTE, "MENU_IMGUI: match setup OPEN");
    }

    /* Opaque backdrop */
    {
        ImDrawList *dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(ImVec2(dialogX, dialogY),
                          ImVec2(dialogX + dialogW, dialogY + dialogH),
                          IM_COL32(8, 8, 16, 255));
    }

    pdguiDrawPdDialog(dialogX, dialogY, dialogW, dialogH, "Match Setup", 1);

    /* Title */
    {
        const char *title = "Match Setup";
        ImDrawList *dl = ImGui::GetWindowDrawList();
        pdguiDrawTextGlow(dialogX + 8.0f, dialogY + 2.0f,
                          dialogW - 16.0f, pdTitleH - 4.0f);
        ImVec2 ts = ImGui::CalcTextSize(title);
        dl->AddText(ImVec2(dialogX + (dialogW - ts.x) * 0.5f,
                           dialogY + (pdTitleH - ts.y) * 0.5f),
                    IM_COL32(255, 255, 255, 255), title);
    }

    ImGui::SetCursorPosY(pdTitleH + ImGui::GetStyle().WindowPadding.y);

    float pad = 8.0f * scale;
    float contentH = dialogH - pdTitleH - 70.0f * scale;

    /* ---- Two-column layout ---- */
    /* Left: Match settings (narrow)   Right: Players panel (wide) */
    float leftW = dialogW * 0.42f;
    float rightW = dialogW * 0.53f;

    /* Left column — match settings (full height, scrollable) */
    ImGui::BeginGroup();
    if (s_NeedsFocus) { ImGui::SetKeyboardFocusHere(0); s_NeedsFocus = false; }
    renderMatchSettings(scale, leftW, contentH);
    ImGui::EndGroup();

    ImGui::SameLine(0, pad);

    /* Right column — player/bot list with +/- buttons (full height) */
    renderPlayersPanel(scale, rightW, contentH);

    /* ---- Footer: Start + Back buttons ---- */
    ImGui::SetCursorPosY(dialogH - 40.0f * scale);
    ImGui::Separator();
    ImGui::Spacing();

    float footerBtnW = 140.0f * scale;
    float footerBtnH = 28.0f * scale;

    /* Center the two buttons */
    float totalBtnW = footerBtnW * 2 + pad;
    ImGui::SetCursorPosX((dialogW - totalBtnW) * 0.5f);

    /* Start Match */
    bool canStart = (g_MatchConfig.numSlots >= 1);
    if (!canStart) ImGui::BeginDisabled();

    if (PdButton("Start Match", ImVec2(footerBtnW, footerBtnH))) {
        sysLogPrintf(LOG_NOTE, "MENU_IMGUI: match setup START MATCH pressed (slots=%d stage='%s')",
                     g_MatchConfig.numSlots, g_MatchConfig.stage_id);
        s_Initialized = false; /* Reset for next time */
        matchStart();
    }

    if (!canStart) ImGui::EndDisabled();

    ImGui::SameLine(0, pad);

    /* Back */
    if (PdButton("Back", ImVec2(footerBtnW, footerBtnH)) ||
        ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) ||
        ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        sysLogPrintf(LOG_NOTE, "MENU_IMGUI: match setup CLOSE via Back/ESC");
        pdguiPlaySound(PDGUI_SND_KBCANCEL);
        s_Initialized = false;
        menuPopDialog();
    }

    ImGui::End();
    return 1;
}

/* ========================================================================
 * Registration
 * ======================================================================== */

extern "C" {

void pdguiMenuMatchSetupRegister(void)
{
    if (!s_Registered) {
        pdguiHotswapRegister(&g_MatchSetupMenuDialog, renderMatchSetup, "Match Setup");
        s_Registered = true;

        /* Phase 6: Screen mini-manifest.
         * Match Setup displays character names and weapon lists — declare
         * the MP language banks it needs.  These are base-game bundled
         * entries today; mod lang banks would go through full lifecycle. */
        {
            static const char *ids[] = {
                "base:lang_mpmenu",    /* MP menu strings (character/mode names) */
                "base:lang_mpweapons", /* Weapon name strings */
            };
            static const u8 types[] = {
                MANIFEST_TYPE_LANG,
                MANIFEST_TYPE_LANG,
            };
            screenManifestRegister(
                (void*)&g_MatchSetupMenuDialog,
                ids, types, 2);
        }
    }
    sysLogPrintf(LOG_NOTE, "pdgui_menu_matchsetup: Registered Match Setup menu");
}

} /* extern "C" */
