/**
 * pdgui_menu_cheats.cpp -- ImGui replacement for the Cheats menu tree.
 *
 * D5 Phase 3 Batch 4.
 *
 * Consolidates 9 legacy cheats dialogs into a tabbed hub:
 *
 *   g_CheatsMenuDialog              -> renderCheatsHub (root)
 *   g_CheatsFunMenuDialog           -> renderCheatsSubRedirect -> TAB_FUN
 *   g_CheatsGameplayMenuDialog      -> renderCheatsSubRedirect -> TAB_GAMEPLAY
 *   g_CheatsSoloWeaponsMenuDialog   -> renderCheatsSubRedirect -> TAB_SOLO_WEAPONS
 *   g_CheatsClassicWeaponsMenuDialog-> renderCheatsSubRedirect -> TAB_CLASSIC_WEAPONS
 *   g_CheatsWeaponsMenuDialog       -> renderCheatsSubRedirect -> TAB_WEAPONS
 *   g_CheatsBuddiesMenuDialog       -> renderCheatsSubRedirect -> TAB_BUDDIES
 *   g_CheatsWarningMenuDialog       -> renderCheatsWarning     (first-use notice)
 *   g_CheatsConfirmUnlockMenuDialog -> renderCheatsConfirmUnlock (Yes/No modal)
 *
 * Design:
 *   - Re-uses existing primitives from pdgui_layout / pdgui_style (scrim,
 *     PD title frame, docked action bar, nav with wrap).
 *   - Delegates ALL state manipulation to legacy cheats.c handlers through
 *     the s194 shadow-struct call-through pattern (solomission.cpp:345).
 *     This keeps bank-mutation logic in one place (cheatCheckboxMenuHandler
 *     handles Marquis/EnemyRockets mutual exclusion; cheatMenuHandleBuddy
 *     handles Velvet/buddy mutual exclusion; cheatMenuHandleTurnOffAllCheats
 *     zeroes both banks).
 *   - Registration via pdguiHotswapRegister, called from pdguiMenusRegisterAll
 *     by way of pdguiMenuCheatsRegister() (declared in pdgui_menus.h).
 *   - Legacy cheatMenuHandleDialog still fires on MENUOP_OPEN/CLOSE regardless
 *     of hot-swap state, so func0f14a52c()/func0f14a560() HUD darken flags
 *     and the piracy check are preserved without duplication.
 *
 * IMPORTANT: C++ file -- must NOT include types.h (#define bool s32 breaks C++).
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
#include "pdgui_nav.h"
#include "pdgui_layout.h"
#include "pdgui.h"        /* langSafe */
#include "system.h"
#include "inputctx.h"

extern "C" {
#include "pdgui_menus.h"  /* for pdguiMenuCheatsRegister declaration */
}

/* =========================================================================
 * Forward declarations -- game symbols (extern "C", no types.h)
 * ========================================================================= */

extern "C" {

/* ---- Dialog definitions ---- */
extern struct menudialogdef g_CheatsMenuDialog;
extern struct menudialogdef g_CheatsFunMenuDialog;
extern struct menudialogdef g_CheatsGameplayMenuDialog;
extern struct menudialogdef g_CheatsSoloWeaponsMenuDialog;
extern struct menudialogdef g_CheatsClassicWeaponsMenuDialog;
extern struct menudialogdef g_CheatsWeaponsMenuDialog;
extern struct menudialogdef g_CheatsBuddiesMenuDialog;
extern struct menudialogdef g_CheatsWarningMenuDialog;
extern struct menudialogdef g_CheatsConfirmUnlockMenuDialog;

/* ---- Menu navigation ---- */
void menuPushDialog(struct menudialogdef *dialogdef);
void menuPopDialog(void);

/* ---- Language (names of cheats etc.) ---- */
char *langGet(s32 textid);
/* Input context g_CtxImGuiMenu declared in inputctx.h (already included). */

/* ---- Menuop constants (only the ones we call through the shadow) ----
 * Values must match src/include/constants.h exactly. */
#define MENUOP_SET 6
#define MENUOP_GET 8

/* ---- s202 shadow menuitem/handlerdata
 * ABI-compatible with the real types in src/include/types.h.  Matches the
 * s194_* pattern in solomission.cpp so the C handlers can be called through
 * function pointers without including types.h from C++. */
union s202_handlerdata {
    u8 _pad[256];
};

struct s202_menuitem {
    u8        type;
    u8        param;       /* cheat_id for cheatCheckboxMenuHandler */
    u32       flags;
    intptr_t  param2;
    intptr_t  param3;
    uintptr_t (*handler)(s32 op, struct s202_menuitem *, union s202_handlerdata *);
};

/* ---- Legacy cheats.c handlers we delegate to ---- */
uintptr_t cheatCheckboxMenuHandler     (s32, struct s202_menuitem *, union s202_handlerdata *);
uintptr_t cheatMenuHandleBuddyCheckbox (s32, struct s202_menuitem *, union s202_handlerdata *);
uintptr_t cheatMenuHandleTurnOffAllCheats(s32, struct s202_menuitem *, union s202_handlerdata *);
/* menuhandlerUnlockEverything in cheats.c is file-static; exposed via
 * gamefileUnlockEverything directly which is what the handler does. */
void gamefileUnlockEverything(void);

/* ---- Cheat metadata (for locked-row tooltip strings) ---- */
struct sc_cheat {
    u16 nametextid;
    u16 time;
    u8  stage_index;
    u8  difficulty;
    u8  flags;
};
extern struct sc_cheat g_Cheats[];
u32 cheatIsUnlocked(s32 cheat_id);

/* ---- Solo stage name lookup for tooltips ---- */
struct sc_solostage {
    u32 stagenum;
    u8  unk04;
    u16 name1;
    u16 name2;
    u16 name3;
    const char *catalog_id;
};
extern struct sc_solostage g_SoloStages[];

} /* extern "C" */

/* =========================================================================
 * Constants (copied locally to avoid pulling in src/include/constants.h)
 * ========================================================================= */

/* Cheat IDs -- from src/include/constants.h.  Kept in-sync here because C++
 * cannot include types.h safely.  If constants.h is bumped, sync here. */
#define SC_CHEAT_HURRICANEFISTS         0
#define SC_CHEAT_CLOAKINGDEVICE         1
#define SC_CHEAT_INVINCIBLE             2
#define SC_CHEAT_ALLGUNS                3
#define SC_CHEAT_UNLIMITEDAMMO          4
#define SC_CHEAT_UNLIMITEDAMMONORELOADS 5
#define SC_CHEAT_SLOMO                  6
#define SC_CHEAT_DKMODE                 7
#define SC_CHEAT_TRENTSMAGNUM           8
#define SC_CHEAT_FARSIGHT               9
#define SC_CHEAT_SMALLJO                10
#define SC_CHEAT_SMALLCHARACTERS        11
#define SC_CHEAT_ENEMYSHIELDS           12
#define SC_CHEAT_JOSHIELD               13
#define SC_CHEAT_SUPERSHIELD            14
#define SC_CHEAT_CLASSICSIGHT           15
#define SC_CHEAT_TEAMHEADSONLY          16
#define SC_CHEAT_PLAYASELVIS            17
#define SC_CHEAT_ENEMYROCKETS           18
#define SC_CHEAT_UNLIMITEDAMMOLAPTOP    19
#define SC_CHEAT_MARQUIS                20
#define SC_CHEAT_PERFECTDARKNESS        21
#define SC_CHEAT_PUGILIST               22
#define SC_CHEAT_HOTSHOT                23
#define SC_CHEAT_HITANDRUN              24
#define SC_CHEAT_ALIEN                  25
#define SC_CHEAT_RTRACKER               26
#define SC_CHEAT_ROCKETLAUNCHER         27
#define SC_CHEAT_SNIPERRIFLE            28
#define SC_CHEAT_XRAYSCANNER            29
#define SC_CHEAT_SUPERDRAGON            30
#define SC_CHEAT_LAPTOPGUN              31
#define SC_CHEAT_PHOENIX                32
#define SC_CHEAT_PSYCHOSISGUN           33
#define SC_CHEAT_PP9I                   34
#define SC_CHEAT_CC13                   35
#define SC_CHEAT_KL01313                36
#define SC_CHEAT_KF7SPECIAL             37
#define SC_CHEAT_ZZT                    38
#define SC_CHEAT_DMC                    39
#define SC_CHEAT_AR53                   40
#define SC_CHEAT_RCP45                  41
#define SC_CHEAT_DUALWIELDALLGUNS       42

#define SC_CHEATFLAG_TIMED       0
#define SC_CHEATFLAG_ALWAYSON    1
#define SC_CHEATFLAG_TRANSFERPAK 2
#define SC_CHEATFLAG_COMPLETION  4
#define SC_CHEATFLAG_FIRINGRANGE 8

/* =========================================================================
 * Module state
 * ========================================================================= */

enum CheatsTab {
    SC_TAB_FUN             = 0,
    SC_TAB_GAMEPLAY        = 1,
    SC_TAB_SOLO_WEAPONS    = 2,
    SC_TAB_CLASSIC_WEAPONS = 3,
    SC_TAB_WEAPONS         = 4,
    SC_TAB_BUDDIES         = 5,
    SC_TAB_COUNT
};

static bool s_Registered      = false;
static s32  s_CheatsTab       = SC_TAB_FUN;   /* currently-shown tab */
static s32  s_PendingTab      = -1;           /* set by redirect renderer */
static bool s_ConfirmUnlockModal = false;     /* inline confirm open? */
static bool s_CheatsHubPushedCtx = false;

/* =========================================================================
 * Per-tab content definition
 * ========================================================================= */

struct CheatRow {
    s32         cheat_id;  /* CHEAT_* constant */
    const char *english;   /* hardcoded English fallback; localized name comes
                            * from langGet(g_Cheats[cheat_id].nametextid) */
};

/* Rows mirror the legacy g_CheatsFunMenuItems etc. exactly (cheats.c:940..). */
static const CheatRow k_FunRows[] = {
    { SC_CHEAT_DKMODE,           "DK Mode" },
    { SC_CHEAT_SMALLJO,          "Small Jo" },
    { SC_CHEAT_SMALLCHARACTERS,  "Small Characters" },
    { SC_CHEAT_TEAMHEADSONLY,    "Team Heads Only" },
    { SC_CHEAT_PLAYASELVIS,      "Play as Elvis" },
    { SC_CHEAT_SLOMO,            "Slo-mo Single Player" },
};

static const CheatRow k_GameplayRows[] = {
    { SC_CHEAT_INVINCIBLE,       "Invincible" },
    { SC_CHEAT_CLOAKINGDEVICE,   "Cloaking Device" },
    { SC_CHEAT_MARQUIS,          "Marquis of Queensbury Rules" },
    { SC_CHEAT_JOSHIELD,         "Jo Shield" },
    { SC_CHEAT_SUPERSHIELD,      "Super Shield" },
    { SC_CHEAT_ENEMYSHIELDS,     "Enemy Shields" },
    { SC_CHEAT_ENEMYROCKETS,     "Enemy Rockets" },
    { SC_CHEAT_PERFECTDARKNESS,  "Perfect Darkness" },
    { SC_CHEAT_DUALWIELDALLGUNS, "Dual Wield All Guns" },
};

static const CheatRow k_SoloWeaponRows[] = {
    { SC_CHEAT_ROCKETLAUNCHER,   "Rocket Launcher" },
    { SC_CHEAT_SNIPERRIFLE,      "Sniper Rifle" },
    { SC_CHEAT_SUPERDRAGON,      "SuperDragon" },
    { SC_CHEAT_LAPTOPGUN,        "Laptop Gun" },
    { SC_CHEAT_PHOENIX,          "Phoenix" },
    { SC_CHEAT_PSYCHOSISGUN,     "Psychosis Gun" },
    { SC_CHEAT_TRENTSMAGNUM,     "Trent's Magnum" },
    { SC_CHEAT_FARSIGHT,         "FarSight" },
};

static const CheatRow k_ClassicWeaponRows[] = {
    { SC_CHEAT_PP9I,             "PP9i" },
    { SC_CHEAT_CC13,             "CC13" },
    { SC_CHEAT_KL01313,          "KL01313" },
    { SC_CHEAT_KF7SPECIAL,       "KF7 Special" },
    { SC_CHEAT_ZZT,              "ZZT (9mm)" },
    { SC_CHEAT_DMC,              "DMC" },
    { SC_CHEAT_AR53,             "AR53" },
    { SC_CHEAT_RCP45,            "RC-P45" },
};

static const CheatRow k_WeaponRows[] = {
    { SC_CHEAT_CLASSICSIGHT,         "Classic Sight" },
    { SC_CHEAT_UNLIMITEDAMMOLAPTOP,  "Unlimited Ammo - Laptop Sentry Gun" },
    { SC_CHEAT_HURRICANEFISTS,       "Hurricane Fists" },
    { SC_CHEAT_UNLIMITEDAMMO,        "Unlimited Ammo" },
    { SC_CHEAT_UNLIMITEDAMMONORELOADS, "Unlimited Ammo, No Reloads" },
    { SC_CHEAT_XRAYSCANNER,          "X-Ray Scanner" },
    { SC_CHEAT_RTRACKER,             "R-Tracker / Weapon Cache Locations" },
    { SC_CHEAT_ALLGUNS,              "All Guns in Solo" },
};

/* Buddies: slot 0 = "Velvet Dark" (no cheat_id, uses param==0 convention
 * per cheatMenuHandleBuddyCheckbox).  Rest are regular cheat_ids. */
struct BuddyRow {
    s32         param;     /* 0 = Velvet, else CHEAT_* */
    const char *english;
};
static const BuddyRow k_BuddyRows[] = {
    { 0,                 "Velvet Dark" },
    { SC_CHEAT_PUGILIST, "Pugilist" },
    { SC_CHEAT_HOTSHOT,  "Hotshot" },
    { SC_CHEAT_HITANDRUN,"Hit and Run" },
    { SC_CHEAT_ALIEN,    "Alien" },
};

struct TabDescriptor {
    const char     *english;
    const CheatRow *rows;
    size_t          count;
};

static const TabDescriptor k_Tabs[SC_TAB_COUNT] = {
    { "Fun",             k_FunRows,           sizeof(k_FunRows)           / sizeof(k_FunRows[0])           },
    { "Gameplay",        k_GameplayRows,      sizeof(k_GameplayRows)      / sizeof(k_GameplayRows[0])      },
    { "Jo Solo Weapons", k_SoloWeaponRows,    sizeof(k_SoloWeaponRows)    / sizeof(k_SoloWeaponRows[0])    },
    { "Classic Weapons", k_ClassicWeaponRows, sizeof(k_ClassicWeaponRows) / sizeof(k_ClassicWeaponRows[0]) },
    { "Weapons",         k_WeaponRows,        sizeof(k_WeaponRows)        / sizeof(k_WeaponRows[0])        },
    { "Buddies",         nullptr,             sizeof(k_BuddyRows)         / sizeof(k_BuddyRows[0])         },
};

/* =========================================================================
 * Legacy-handler helpers (s202 call-through pattern)
 * ========================================================================= */

static inline bool sc_GetCheatEnabled(s32 cheat_id)
{
    struct s202_menuitem it{};
    it.param = (u8)cheat_id;
    return cheatCheckboxMenuHandler(MENUOP_GET, &it, nullptr) != 0;
}

static inline void sc_ToggleCheat(s32 cheat_id)
{
    struct s202_menuitem it{};
    it.param = (u8)cheat_id;
    cheatCheckboxMenuHandler(MENUOP_SET, &it, nullptr);
}

static inline bool sc_GetBuddyEnabled(s32 param)
{
    struct s202_menuitem it{};
    it.param = (u8)param;
    return cheatMenuHandleBuddyCheckbox(MENUOP_GET, &it, nullptr) != 0;
}

static inline void sc_SelectBuddy(s32 param)
{
    struct s202_menuitem it{};
    it.param = (u8)param;
    cheatMenuHandleBuddyCheckbox(MENUOP_SET, &it, nullptr);
}

static inline void sc_TurnOffAllCheats(void)
{
    struct s202_menuitem it{};
    cheatMenuHandleTurnOffAllCheats(MENUOP_SET, &it, nullptr);
}

/* =========================================================================
 * Row rendering helpers
 * =========================================================================
 *
 * One row per cheat.  Layout:
 *
 *    [Locked/ ✓]   Cheat Name
 *
 * Locked rows are disabled and show a hover tooltip describing how to unlock.
 * Unlocked rows are checkboxes backed by g_CheatsEnabledBank0/1 via the
 * legacy handler (so Marquis/EnemyRockets mutual exclusion stays single-
 * sourced). */

static const char *sc_localizedCheatName(s32 cheat_id, const char *fallback)
{
    /* g_Cheats layout is stable; nametextid is a u16 lang id.  langSafe
     * tolerates NULL/empty and returns "". */
    const char *name = langSafe((s32)g_Cheats[cheat_id].nametextid);
    if (name && name[0]) return name;
    return fallback;
}

static const char *sc_stageName(s32 stage_index, char *buf, size_t bufn)
{
    const char *n1 = langSafe((s32)g_SoloStages[stage_index].name1);
    const char *n2 = langSafe((s32)g_SoloStages[stage_index].name2);
    snprintf(buf, bufn, "%s%s", n1 ? n1 : "", n2 ? n2 : "");
    return buf;
}

static void sc_buildUnlockTooltip(s32 cheat_id, char *out, size_t outn)
{
    const struct sc_cheat *c = &g_Cheats[cheat_id];

    char stagebuf[128]; stagebuf[0] = '\0';
    if (c->stage_index < 21 /* NUM_SOLOSTAGES */) {
        sc_stageName(c->stage_index, stagebuf, sizeof(stagebuf));
    }

    const char *diffName = "Agent";
    if (c->difficulty == 1) diffName = "Special Agent";
    else if (c->difficulty == 2) diffName = "Perfect Agent";

    if (c->flags & SC_CHEATFLAG_FIRINGRANGE) {
        snprintf(out, outn,
                 "Locked.\nWin gold medals on the firing range to unlock.");
    } else if (c->flags & SC_CHEATFLAG_COMPLETION) {
        snprintf(out, outn,
                 "Locked.\nComplete %s on Agent to unlock.", stagebuf);
    } else {
        /* Timed */
        s32 mins = c->time / 60;
        s32 secs = c->time % 60;
        snprintf(out, outn,
                 "Locked.\nComplete %s on %s in under %d:%02d to unlock.",
                 stagebuf, diffName, mins, secs);
    }

    if (c->flags & SC_CHEATFLAG_TRANSFERPAK) {
        /* Transfer-pak alt-unlock doesn't apply on PC port (no GB peripheral),
         * but the message is still accurate for the canonical unlock path. */
    }
}

/* =========================================================================
 * Main hub renderer -- the tabbed cheats screen
 * ========================================================================= */

static s32 renderCheatsHub(struct menudialog *dialog,
                            struct menu *menu,
                            s32 winW, s32 winH)
{
    (void)dialog; (void)menu; (void)winW; (void)winH;

    /* Pending tab set by a sub-dialog redirect -- apply once at entry.
     * s_ForceSelectTab is true for exactly one frame after a programmatic
     * tab change (redirect or bumper press). SetSelected only fires on
     * that frame, so user clicks work normally on all other frames. */
    static bool s_ForceSelectTab = false;
    if (s_PendingTab >= 0 && s_PendingTab < SC_TAB_COUNT) {
        s_CheatsTab = s_PendingTab;
        s_PendingTab = -1;
        s_ForceSelectTab = true;
    }

    pdguiPopupDarkenBehind(0.55f);

    float mw  = pdguiMenuWidth()  * 0.70f;
    float mh  = pdguiMenuHeight() * 0.80f;
    ImVec2 pos = pdguiCenterPos(mw, mh);

    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(ImVec2(mw, mh));

    ImGuiWindowFlags wf = ImGuiWindowFlags_NoResize
                        | ImGuiWindowFlags_NoMove
                        | ImGuiWindowFlags_NoCollapse
                        | ImGuiWindowFlags_NoSavedSettings
                        | ImGuiWindowFlags_NoTitleBar
                        | ImGuiWindowFlags_NoBackground;

    if (!ImGui::Begin("##cheats_hub", nullptr, wf)) {
        ImGui::End();
        return 1;
    }

    if (ImGui::IsWindowAppearing()) {
        ImGui::SetWindowFocus();
        pdguiPlaySound(PDGUI_SND_OPENDIALOG);
        sysLogPrintf(LOG_NOTE, "MENU_IMGUI: Cheats hub OPEN (tab=%d)",
                     (int)s_CheatsTab);
        s_CheatsHubPushedCtx = false;
        if (!inputCtxIsActive(&g_CtxImGuiMenu)) {
            inputCtxPush(&g_CtxImGuiMenu);
            s_CheatsHubPushedCtx = true;
        }
    }

    /* PD-style title frame */
    float titleH = pdguiScale(39.0f);
    pdguiDrawPdDialog(pos.x, pos.y, mw, mh, "Cheats", 1);
    ImGui::SetCursorPosY(titleH + ImGui::GetStyle().WindowPadding.y);

    /* B / Escape closes the hub entirely. */
    if (!ImGui::IsWindowAppearing() && !s_ConfirmUnlockModal &&
        (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) ||
         ImGui::IsKeyPressed(ImGuiKey_Escape, false))) {
        pdguiPlaySound(PDGUI_SND_KBCANCEL);
        if (s_CheatsHubPushedCtx && inputCtxIsActive(&g_CtxImGuiMenu)) {
            inputCtxPopDeferred(&g_CtxImGuiMenu);
        }
        s_CheatsHubPushedCtx = false;
        menuPopDialog();
        ImGui::End();
        return 1;
    }

    /* Body -- reserve space for docked action bar. */
    float avail = ImGui::GetContentRegionAvail().y;
    float bodyH = pdguiBodyHeightForActionBar(avail);

    if (ImGui::BeginChild("##cheats_body", ImVec2(0, bodyH), false,
                          ImGuiWindowFlags_NoBackground)) {

        /* Bumper (LB/RB) tab cycling via PageUp/PageDown */
        if (ImGui::IsKeyPressed(ImGuiKey_PageUp, false)) {
            s_CheatsTab = (s_CheatsTab - 1 + SC_TAB_COUNT) % SC_TAB_COUNT;
            s_ForceSelectTab = true;
            pdguiPlaySound(PDGUI_SND_SWIPE);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_PageDown, false)) {
            s_CheatsTab = (s_CheatsTab + 1) % SC_TAB_COUNT;
            s_ForceSelectTab = true;
            pdguiPlaySound(PDGUI_SND_SWIPE);
        }

        /* Tab strip — SetSelected only fires on the frame after a
         * programmatic tab change (redirect or bumper press). On all
         * other frames, ImGui handles tab clicks normally. */
        if (ImGui::BeginTabBar("##cheats_tabs", ImGuiTabBarFlags_None)) {
            for (s32 i = 0; i < SC_TAB_COUNT; i++) {
                ImGuiTabItemFlags tflags = ImGuiTabItemFlags_None;
                if (s_ForceSelectTab && i == s_CheatsTab) {
                    tflags |= ImGuiTabItemFlags_SetSelected;
                }
                if (ImGui::BeginTabItem(k_Tabs[i].english, nullptr, tflags)) {
                    if (s_CheatsTab != i) {
                        s_CheatsTab = i;
                        pdguiPlaySound(PDGUI_SND_SUBFOCUS);
                    }

                    /* Panel body for this tab */
                    ImGui::Dummy(ImVec2(0, pdguiScale(6.0f)));

                    if (i == SC_TAB_BUDDIES) {
                        /* Buddy panel: radio behaviour. Velvet = "none". */
                        for (size_t r = 0; r < (sizeof(k_BuddyRows) / sizeof(k_BuddyRows[0])); r++) {
                            const BuddyRow &br = k_BuddyRows[r];

                            /* Locked check: Velvet (param==0) is always
                             * available; others need their cheat unlocked. */
                            bool locked = (br.param != 0) && (cheatIsUnlocked(br.param) == 0);
                            bool enabled = sc_GetBuddyEnabled(br.param);

                            /* Name lookup.  Velvet uses a literal lang id in
                             * the legacy menu (L_MPWEAPONS_117); the other
                             * four use cheatGetNameIfUnlocked.  We fall back
                             * on the English hardcoded label. */
                            const char *name = br.english;
                            if (br.param != 0) {
                                name = sc_localizedCheatName(br.param, br.english);
                                if (locked) name = "----------";
                            }

                            ImGui::PushID((int)r);
                            if (locked) {
                                /* Disabled row -- no interaction. */
                                ImGui::BeginDisabled();
                                bool dummy = false;
                                ImGui::RadioButton(name, dummy);
                                ImGui::EndDisabled();
                                if (ImGui::IsItemHovered()) {
                                    char tip[256];
                                    sc_buildUnlockTooltip(br.param, tip, sizeof(tip));
                                    ImGui::SetTooltip("%s", tip);
                                }
                            } else {
                                if (ImGui::RadioButton(name, enabled)) {
                                    sc_SelectBuddy(br.param);
                                    pdguiPlaySound(PDGUI_SND_SELECT);
                                }
                            }
                            ImGui::PopID();
                        }
                    } else {
                        /* Regular cheat tabs: checkbox list. */
                        const CheatRow *rows = k_Tabs[i].rows;
                        size_t count        = k_Tabs[i].count;

                        for (size_t r = 0; r < count; r++) {
                            const CheatRow &cr = rows[r];
                            bool locked  = (cheatIsUnlocked(cr.cheat_id) == 0);
                            bool enabled = sc_GetCheatEnabled(cr.cheat_id);
                            const char *name = locked
                                ? "----------"
                                : sc_localizedCheatName(cr.cheat_id, cr.english);

                            ImGui::PushID((int)(r + 0x100));
                            if (locked) {
                                ImGui::BeginDisabled();
                                bool dummy = false;
                                ImGui::Checkbox(name, &dummy);
                                ImGui::EndDisabled();
                                if (ImGui::IsItemHovered()) {
                                    char tip[256];
                                    sc_buildUnlockTooltip(cr.cheat_id, tip, sizeof(tip));
                                    ImGui::SetTooltip("%s", tip);
                                }
                            } else {
                                bool before = enabled;
                                if (ImGui::Checkbox(name, &enabled)) {
                                    if (before != enabled) {
                                        sc_ToggleCheat(cr.cheat_id);
                                        pdguiPlaySound(enabled
                                            ? PDGUI_SND_TOGGLEON
                                            : PDGUI_SND_TOGGLEOFF);
                                    }
                                }
                            }
                            ImGui::PopID();
                        }
                    }

                    ImGui::EndTabItem();
                }
            }
            ImGui::EndTabBar();
            s_ForceSelectTab = false; /* consumed — let user clicks work normally */
        }
    }
    ImGui::EndChild();

    /* Docked action bar -- "Turn Off All" / "Unlock All..." / "Back" */
    bool wantClose        = false;
    bool wantTurnOffAll   = false;
    bool wantUnlockModal  = false;

    if (pdguiBeginActionBar("##cheats_ab")) {
        float barW  = ImGui::GetContentRegionAvail().x;
        float btnW  = barW / 3.0f;

        if (pdguiActionBarButton("Turn Off All", 0, btnW)) {
            wantTurnOffAll = true;
        }
        ImGui::SameLine();
        if (pdguiActionBarButton("Unlock All...", 0, btnW)) {
            wantUnlockModal = true;
        }
        ImGui::SameLine();
        if (pdguiActionBarButton("Back", 1, ImGui::GetContentRegionAvail().x)) {
            wantClose = true;
        }
    }
    pdguiEndActionBar();

    if (wantTurnOffAll) {
        sc_TurnOffAllCheats();
        pdguiPlaySound(PDGUI_SND_SELECT);
        sysLogPrintf(LOG_NOTE, "MENU_IMGUI: Cheats -- Turn Off All cheats");
    }
    if (wantUnlockModal) {
        /* Route through the legacy DANGER dialog so cheatMenuHandleDialog
         * etc. fire their OPEN/CLOSE side effects consistently.  Our
         * renderCheatsConfirmUnlock will catch it. */
        menuPushDialog(&g_CheatsConfirmUnlockMenuDialog);
    }
    if (wantClose) {
        if (s_CheatsHubPushedCtx && inputCtxIsActive(&g_CtxImGuiMenu)) {
            inputCtxPopDeferred(&g_CtxImGuiMenu);
        }
        s_CheatsHubPushedCtx = false;
        menuPopDialog();
    }

    pdguiNavTickWrap();
    ImGui::End();
    return 1;
}

/* =========================================================================
 * Sub-dialog redirect: pop self, request tab switch on the live hub.
 * =========================================================================
 *
 * Legacy code pushes one of the six sub-dialogs directly (e.g. via an old
 * script or a pause-menu entry we haven't audited).  Our hub is already a
 * replacement for g_CheatsMenuDialog, so we don't want a second hub on the
 * stack -- we pop the sub-dialog and flip s_PendingTab so the already-open
 * hub switches tabs next frame. */

static s32 sc_tabForDialog(struct menudialogdef *def)
{
    if (def == &g_CheatsFunMenuDialog)            return SC_TAB_FUN;
    if (def == &g_CheatsGameplayMenuDialog)       return SC_TAB_GAMEPLAY;
    if (def == &g_CheatsSoloWeaponsMenuDialog)    return SC_TAB_SOLO_WEAPONS;
    if (def == &g_CheatsClassicWeaponsMenuDialog) return SC_TAB_CLASSIC_WEAPONS;
    if (def == &g_CheatsWeaponsMenuDialog)        return SC_TAB_WEAPONS;
    if (def == &g_CheatsBuddiesMenuDialog)        return SC_TAB_BUDDIES;
    return -1;
}

static s32 renderCheatsSubRedirect(struct menudialog *dialog,
                                    struct menu *menu,
                                    s32 winW, s32 winH)
{
    (void)menu; (void)winW; (void)winH;

    /* struct menudialog's first field is menudialogdef* -- see
     * renderCiSettingsRedirect in pdgui_menu_mainmenu.cpp:2843. */
    struct menudialogdef *def = *(struct menudialogdef **)((u8 *)dialog);

    static struct menudialogdef *s_LastSeen = nullptr;
    if (ImGui::IsWindowAppearing() || s_LastSeen != def) {
        s32 tab = sc_tabForDialog(def);
        if (tab >= 0) {
            s_PendingTab = tab;
            sysLogPrintf(LOG_NOTE,
                "MENU_IMGUI: Cheats sub-dialog redirect (dialog=%p tab=%d)",
                (void *)def, (int)tab);
        }
        s_LastSeen = def;
    }

    /* Draw nothing -- the already-open hub will pick up s_PendingTab next
     * frame.  Pop self so the stack returns to g_CheatsMenuDialog (which our
     * hub renders). */
    menuPopDialog();
    s_LastSeen = nullptr;
    return 1;
}

/* =========================================================================
 * First-use warning modal
 * =========================================================================
 *
 * g_CheatsWarningMenuDialog is a SUCCESS-type modal that shows:
 *   "If you activate any cheats, then you will be unable to progress further
 *    in the game while those cheats are active."
 *
 * Legacy items: LABEL + OK + Cancel.  Our replacement draws the same text
 * directly with a docked OK / Cancel action bar. */

static s32 renderCheatsWarning(struct menudialog *dialog,
                                struct menu *menu,
                                s32 winW, s32 winH)
{
    (void)dialog; (void)menu; (void)winW; (void)winH;

    pdguiPopupDarkenBehind(0.55f);

    float mw = pdguiScale(640.0f);
    float mh = pdguiScale(260.0f);
    ImVec2 pos = pdguiCenterPos(mw, mh);

    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(ImVec2(mw, mh));

    ImGuiWindowFlags wf = ImGuiWindowFlags_NoResize
                        | ImGuiWindowFlags_NoMove
                        | ImGuiWindowFlags_NoCollapse
                        | ImGuiWindowFlags_NoSavedSettings
                        | ImGuiWindowFlags_NoTitleBar
                        | ImGuiWindowFlags_NoBackground;

    if (!ImGui::Begin("##cheats_warning", nullptr, wf)) {
        ImGui::End();
        return 1;
    }

    if (ImGui::IsWindowAppearing()) {
        pdguiPlaySound(PDGUI_SND_OPENDIALOG);
    }

    float titleH = pdguiScale(39.0f);
    pdguiDrawPdDialog(pos.x, pos.y, mw, mh, "Cheats", 1);
    ImGui::SetCursorPosY(titleH + ImGui::GetStyle().WindowPadding.y);

    ImGui::TextWrapped(
        "If you activate any cheats, you will be unable to progress "
        "further in the game while those cheats are active.");

    float avail = ImGui::GetContentRegionAvail().y;
    float bodyH = pdguiBodyHeightForActionBar(avail);
    ImGui::Dummy(ImVec2(0, bodyH - ImGui::GetStyle().ItemSpacing.y));

    bool wantClose = false;

    if (pdguiBeginActionBar("##cheats_warn_ab")) {
        float barW = ImGui::GetContentRegionAvail().x;
        float half = barW * 0.5f;
        if (pdguiActionBarButton("OK", 1, half)) {
            wantClose = true;
        }
        ImGui::SameLine();
        if (pdguiActionBarButton("Cancel", 0, ImGui::GetContentRegionAvail().x)) {
            wantClose = true;
        }
    }
    pdguiEndActionBar();

    if (!ImGui::IsWindowAppearing() &&
        (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) ||
         ImGui::IsKeyPressed(ImGuiKey_Escape, false) ||
         ImGui::IsKeyPressed(ImGuiKey_Enter, false))) {
        wantClose = true;
        pdguiPlaySound(PDGUI_SND_KBCANCEL);
    }

    if (wantClose) {
        menuPopDialog();
    }

    ImGui::End();
    return 1;
}

/* =========================================================================
 * Unlock-everything confirmation (DANGER modal)
 * ========================================================================= */

static s32 renderCheatsConfirmUnlock(struct menudialog *dialog,
                                      struct menu *menu,
                                      s32 winW, s32 winH)
{
    (void)dialog; (void)menu; (void)winW; (void)winH;

    pdguiPopupDarkenBehind(0.70f);

    float mw = pdguiScale(620.0f);
    float mh = pdguiScale(300.0f);
    ImVec2 pos = pdguiCenterPos(mw, mh);

    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(ImVec2(mw, mh));

    ImGuiWindowFlags wf = ImGuiWindowFlags_NoResize
                        | ImGuiWindowFlags_NoMove
                        | ImGuiWindowFlags_NoCollapse
                        | ImGuiWindowFlags_NoSavedSettings
                        | ImGuiWindowFlags_NoTitleBar
                        | ImGuiWindowFlags_NoBackground;

    if (!ImGui::Begin("##cheats_unlock_confirm", nullptr, wf)) {
        ImGui::End();
        return 1;
    }

    if (ImGui::IsWindowAppearing()) {
        pdguiPlaySound(PDGUI_SND_OPENDIALOG);
    }

    float titleH = pdguiScale(39.0f);
    /* Style 2 = DANGER (red) per pdguiDrawPdDialog palette */
    pdguiDrawPdDialog(pos.x, pos.y, mw, mh, "Warning", 2);
    ImGui::SetCursorPosY(titleH + ImGui::GetStyle().WindowPadding.y);

    ImGui::TextWrapped("Are you sure?");
    ImGui::Spacing();
    ImGui::TextWrapped(
        "This will overwrite any progress saved to the current profile.");
    ImGui::Spacing();
    ImGui::TextWrapped(
        "Unlocks all cheats, weapons, missions, challenges and combat "
        "simulator items.");

    float avail = ImGui::GetContentRegionAvail().y;
    float bodyH = pdguiBodyHeightForActionBar(avail);
    ImGui::Dummy(ImVec2(0, bodyH - ImGui::GetStyle().ItemSpacing.y));

    bool doYes = false;
    bool doNo  = false;

    if (pdguiBeginActionBar("##cheats_unlock_ab")) {
        float barW = ImGui::GetContentRegionAvail().x;
        float half = barW * 0.5f;
        if (pdguiActionBarButton("No", 1, half)) {
            doNo = true;
        }
        ImGui::SameLine();
        if (pdguiActionBarButton("Yes", 0, ImGui::GetContentRegionAvail().x)) {
            doYes = true;
        }
    }
    pdguiEndActionBar();

    if (!ImGui::IsWindowAppearing() &&
        (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) ||
         ImGui::IsKeyPressed(ImGuiKey_Escape, false))) {
        doNo = true;
        pdguiPlaySound(PDGUI_SND_KBCANCEL);
    }

    if (doYes) {
        /* Legacy menuhandlerUnlockEverything is file-static in cheats.c; it
         * just calls gamefileUnlockEverything().  We call the same function
         * directly and preserve zero-function-loss. */
        gamefileUnlockEverything();
        sysLogPrintf(LOG_NOTE, "MENU_IMGUI: Cheats -- Unlock Everything applied");
        pdguiPlaySound(PDGUI_SND_SELECT);
        menuPopDialog();
    } else if (doNo) {
        menuPopDialog();
    }

    ImGui::End();
    return 1;
}

/* =========================================================================
 * Registration
 * ========================================================================= */

extern "C" {

void pdguiMenuCheatsRegister(void)
{
    if (s_Registered) return;
    s_Registered = true;

    pdguiHotswapRegister(&g_CheatsMenuDialog,
                          renderCheatsHub,
                          "Cheats Hub");
    pdguiHotswapRegister(&g_CheatsFunMenuDialog,
                          renderCheatsSubRedirect,
                          "Cheats Fun -> hub.TAB_FUN");
    pdguiHotswapRegister(&g_CheatsGameplayMenuDialog,
                          renderCheatsSubRedirect,
                          "Cheats Gameplay -> hub.TAB_GAMEPLAY");
    pdguiHotswapRegister(&g_CheatsSoloWeaponsMenuDialog,
                          renderCheatsSubRedirect,
                          "Cheats Solo Weapons -> hub.TAB_SOLO_WEAPONS");
    pdguiHotswapRegister(&g_CheatsClassicWeaponsMenuDialog,
                          renderCheatsSubRedirect,
                          "Cheats Classic Weapons -> hub.TAB_CLASSIC_WEAPONS");
    pdguiHotswapRegister(&g_CheatsWeaponsMenuDialog,
                          renderCheatsSubRedirect,
                          "Cheats Weapons -> hub.TAB_WEAPONS");
    pdguiHotswapRegister(&g_CheatsBuddiesMenuDialog,
                          renderCheatsSubRedirect,
                          "Cheats Buddies -> hub.TAB_BUDDIES");

    pdguiHotswapRegister(&g_CheatsWarningMenuDialog,
                          renderCheatsWarning,
                          "Cheats Warning (first-use notice)");
    pdguiHotswapRegister(&g_CheatsConfirmUnlockMenuDialog,
                          renderCheatsConfirmUnlock,
                          "Cheats Confirm Unlock Everything");

    sysLogPrintf(LOG_NOTE,
        "pdgui_menu_cheats: Registered (hub + 6 sub-redirects + 2 modals)");
}

} /* extern "C" */
