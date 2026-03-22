/**
 * pdgui_menu_matchsetup.cpp -- ImGui match setup / lobby screen.
 *
 * Unified match configuration screen for both local play and network lobby.
 * The party leader (or local player in offline mode) configures:
 *   - Character slots (players + bots, up to 32: 8 players + 24 bots)
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

#include "imgui/imgui.h"
#include "pdgui_hotswap.h"
#include "pdgui_style.h"
#include "pdgui_audio.h"
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
char *langGet(s32 textid);

/* Match config (from matchsetup.c — must match layout exactly) */
#define MAX_PLAYER_NAME 32
#define MAX_PLAYERS     8
#define MAX_BOTS        24
#define MAX_MPCHRS      (MAX_PLAYERS + MAX_BOTS)

#define SLOT_EMPTY    0
#define SLOT_PLAYER   1
#define SLOT_BOT      2

#define MATCH_MAX_SLOTS MAX_MPCHRS  /* 32 — must match matchsetup.c */

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
    u8 weapons[6];
    s8 weaponSetIndex;
    u8 numSlots;
};

extern struct matchconfig g_MatchConfig;
void matchConfigInit(void);
s32 matchConfigAddBot(u8 botType, u8 botDifficulty, u8 headnum, u8 bodynum, const char *name);
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

/* Arena list is populated dynamically from modmgrGetTotalArenas() / modmgrGetArena()
 * at init time. This includes base game MP arenas, solo mission stages used in MP,
 * GoldenEye X maps, bonus maps, and any mod-added arenas. */

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
    if (name) *name = langGet(arena->name);
    return 1;
}

/* ========================================================================
 * Local state
 * ======================================================================== */

static bool s_Registered = false;
static bool s_Initialized = false;
static s32 s_ArenaIndex = 0;         /* Index into modmgr arena list */
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
 * Helper: find arena index from stagenum
 * ======================================================================== */

static s32 findArenaIndex(u8 stagenum)
{
    s32 total = modmgrGetTotalArenas();
    for (s32 i = 0; i < total; i++) {
        struct mparena *arena = modmgrGetArena(i);
        if (arena->stagenum == (s16)stagenum) return i;
    }
    return 0;
}

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
static bool s_BotPopupOpen = false;
static s32 s_BotPopupSlot = -1;

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

            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "Edit Bot");
            ImGui::Separator();
            ImGui::Spacing();

            /* Name */
            ImGui::InputText("Name", bot->name, sizeof(bot->name));

            /* Character body */
            {
                u32 numBodies = mpGetNumBodies();
                const char *bodyName = mpGetBodyName(bot->bodynum);
                char bodyLabel[64];
                snprintf(bodyLabel, sizeof(bodyLabel), "%s",
                         bodyName ? bodyName : "???");

                if (ImGui::BeginCombo("Character", bodyLabel)) {
                    for (u32 b = 0; b < numBodies && b < 200; b++) {
                        const char *bName = mpGetBodyName((u8)b);
                        if (!bName || !bName[0]) continue;
                        char itemLabel[64];
                        snprintf(itemLabel, sizeof(itemLabel), "%s##body%d", bName, b);
                        bool isSel = (bot->bodynum == (u8)b);
                        if (ImGui::Selectable(itemLabel, isSel)) {
                            bot->bodynum = (u8)b;
                            bot->headnum = (u8)b;
                            pdguiPlaySound(PDGUI_SND_SUBFOCUS);
                        }
                        if (isSel) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
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

            ImGui::Spacing();
            if (PdButton("Done", ImVec2(80.0f * scale, 0))) {
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }

    ImGui::EndChild(); /* player_scroll */

    /* ---- Bot +/- buttons docked to bottom ---- */
    ImGui::Spacing();

    /* Count current bots */
    s32 botCount = 0;
    for (s32 i = 0; i < g_MatchConfig.numSlots; i++) {
        if (g_MatchConfig.slots[i].type == SLOT_BOT) botCount++;
    }

    float btnW = 32.0f * scale;
    float btnH = 24.0f * scale;

    /* Right-align the bot count + buttons */
    float botLabelW = ImGui::CalcTextSize("Bots: 00").x;
    float btnPad = 6.0f * scale;
    float totalW = botLabelW + btnPad + btnW * 2 + 4.0f * scale;
    ImGui::SetCursorPosX(panelW - totalW - ImGui::GetStyle().WindowPadding.x);

    ImGui::AlignTextToFramePadding();
    ImGui::Text("Bots: %02d", botCount);
    ImGui::SameLine(0, btnPad);

    /* + button */
    bool canAdd = (g_MatchConfig.numSlots < MATCH_MAX_SLOTS);
    if (!canAdd) ImGui::BeginDisabled();
    if (PdButton("+##addbot", ImVec2(btnW, btnH))) {
        s32 newIdx = matchConfigAddBot(
            (u8)s_AddBotType, (u8)s_AddBotDiff,
            0, BODY_DARK_COMBAT, NULL);
        if (newIdx >= 0) selectionSet(newIdx);
    }
    if (!canAdd) ImGui::EndDisabled();

    ImGui::SameLine(0, 2.0f * scale);

    /* - button (remove last bot) */
    bool canRemove = (botCount > 0);
    if (!canRemove) ImGui::BeginDisabled();
    if (PdButton("-##rmbot", ImVec2(btnW, btnH))) {
        /* Remove last bot in the list */
        for (s32 i = g_MatchConfig.numSlots - 1; i >= 0; i--) {
            if (g_MatchConfig.slots[i].type == SLOT_BOT) {
                matchConfigRemoveSlot(i);
                pdguiPlaySound(PDGUI_SND_KBCANCEL);
                break;
            }
        }
    }
    if (!canRemove) ImGui::EndDisabled();

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

    /* Arena/Stage — dynamic list from modmgr (base + mod arenas) */
    {
        s32 totalArenas = modmgrGetTotalArenas();
        struct mparena *curArena = modmgrGetArena(s_ArenaIndex);
        const char *curArenaName = langGet(curArena->name);

        if (ImGui::BeginCombo("Arena", curArenaName ? curArenaName : "???")) {
            for (s32 i = 0; i < totalArenas; i++) {
                struct mparena *arena = modmgrGetArena(i);
                /* Skip arenas that are locked behind unlockable features */
                if (!challengeIsFeatureUnlocked(arena->requirefeature)) continue;

                const char *arenaName = langGet(arena->name);
                if (!arenaName || !arenaName[0]) continue;

                bool isSel = (i == s_ArenaIndex);
                char arenaLabel[96];
                snprintf(arenaLabel, sizeof(arenaLabel), "%s##arena%d", arenaName, i);
                if (ImGui::Selectable(arenaLabel, isSel)) {
                    s_ArenaIndex = i;
                    g_MatchConfig.stagenum = (u8)arena->stagenum;
                    pdguiPlaySound(PDGUI_SND_SUBFOCUS);
                }
                if (isSel) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
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
        s_ArenaIndex = findArenaIndex(g_MatchConfig.stagenum);
        selectionClear();
        s_Tab = 0;
        s_Initialized = true;
    }

    float scale = (float)winH / 480.0f;
    float dialogW = 640.0f * scale;
    float dialogH = 460.0f * scale;
    float dialogX = ((float)winW - dialogW) * 0.5f;
    float dialogY = ((float)winH - dialogH) * 0.5f;
    float pdTitleH = 26.0f * scale;

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
        sysLogPrintf(LOG_NOTE, "MATCHSETUP: user pressed Start Match");
        s_Initialized = false; /* Reset for next time */
        matchStart();
    }

    if (!canStart) ImGui::EndDisabled();

    ImGui::SameLine(0, pad);

    /* Back */
    if (PdButton("Back", ImVec2(footerBtnW, footerBtnH)) ||
        ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) ||
        ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
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
    }
    sysLogPrintf(LOG_NOTE, "pdgui_menu_matchsetup: Registered Match Setup menu");
}

} /* extern "C" */
