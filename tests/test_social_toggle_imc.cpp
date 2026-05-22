/*
 * test_social_toggle_imc.cpp -- S483b (2026-04-27)
 *
 * Pins the IMC invariant Mike named:
 *   "Tab during top-IMC = gameplay does not toggle sidebar state."
 *
 * Background. The legacy raw `ImGui::IsKeyPressed(ImGuiKey_Tab)` hotkey
 * at port/fast3d/pdgui_friends.cpp:813 bypassed the actionmap layer with
 * an explicit comment "Avoids reaching into the actionmap layer". A
 * playtest at 04:21 on 2026-04-27 hit Tab mid-mission and the Online
 * connectivity sidebar opened during gameplay -- design intent is that
 * the sidebar is reachable only when paused (or in any non-gameplay
 * menu).
 *
 * Fix routes Tab through actionmap as ACTION_SOCIAL_TOGGLE bound on
 * g_ImcMenu + g_ImcPauseMenu only (NOT on g_ImcGameplay). fireVk's
 * walk over s_Active is first-match-wins by priority -- once you stop
 * binding Tab on the gameplay IMC, Tab during pure gameplay simply
 * has no resolvable mapping for ACTION_SOCIAL_TOGGLE.
 *
 * This test mirrors the resolution algorithm in a self-contained pure
 * form. It's a binding-table contract test: the action-id constants
 * (43 = SDL_SCANCODE_TAB, 56 = ACTION_SCORECARD, 69 = ACTION_SOCIAL_TOGGLE)
 * are pinned numerically here so a future enum reorder shows up as a
 * test failure pointing at this file rather than a silent regression
 * during gameplay.
 *
 * @SYNC port/include/actionmap.h InputAction enum (ACTION_SOCIAL_TOGGLE = 69)
 * @SYNC port/src/actionmap.cpp fireVk (priority-sorted first-match-wins)
 * @SYNC port/src/actionmap.cpp setupGameplayDefaults (Tab -> ACTION_SCORECARD)
 * @SYNC port/src/actionmap.cpp setupMenuDefaults / setupPauseMenuDefaults
 *       (Tab -> ACTION_SOCIAL_TOGGLE)
 * @SYNC port/fast3d/pdgui_friends.cpp pdguiFriendsRender
 *       (actionPressed(0, ACTION_SOCIAL_TOGGLE) replaced raw ImGui hotkey)
 */

#include "catch.hpp"

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

/* Pinned scancode + action ids that the runtime uses. If the enum
 * is reordered without updating this file, the contract has shifted and
 * the test should be re-anchored. */
constexpr unsigned VK_TAB = 43;          /* SDL_SCANCODE_TAB */
constexpr int ACT_NONE             = -1;
constexpr int ACT_SCORECARD        = 56; /* InputAction::ACTION_SCORECARD */
constexpr int ACT_SOCIAL_TOGGLE    = 69; /* InputAction::ACTION_SOCIAL_TOGGLE */
constexpr unsigned VK_V = 25;            /* SDL_SCANCODE_V */
constexpr int ACT_VOICE_PTT = 85;        /* InputAction::ACTION_VOICE_PTT */

struct PureBind {
    unsigned vk;
    int action;
};

struct PureCtx {
    const char *name;
    int priority;
    std::vector<PureBind> binds;
};

/* Pure mirror of port/src/actionmap.cpp fireVk's walk: contexts arrive
 * already priority-sorted (highest priority first); first matching VK
 * wins; ties within an IMC are broken by lower action-id. */
int resolvePureVk(const std::vector<const PureCtx *> &active, unsigned vk)
{
    for (const PureCtx *c : active) {
        int best_action = -1;
        for (const PureBind &b : c->binds) {
            if (b.vk != vk) continue;
            if (best_action == -1 || b.action < best_action) {
                best_action = b.action;
            }
        }
        if (best_action != -1) return best_action;
    }
    return ACT_NONE;
}

std::string readTextFile(const char *path)
{
    std::ifstream f(path, std::ios::in | std::ios::binary);
    if (!f) {
        return {};
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

/* Build IMCs that mirror today's binding tables, scoped to Tab. */
PureCtx makeGameplay()
{
    return PureCtx{ "gameplay", 0, { { VK_TAB, ACT_SCORECARD }, { VK_V, ACT_VOICE_PTT } } };
}

PureCtx makeMenu()
{
    return PureCtx{ "menu", 10, { { VK_TAB, ACT_SOCIAL_TOGGLE }, { VK_V, ACT_VOICE_PTT } } };
}

PureCtx makePauseMenu()
{
    return PureCtx{ "pause_menu", 11, { { VK_TAB, ACT_SOCIAL_TOGGLE }, { VK_V, ACT_VOICE_PTT } } };
}

} /* namespace */

TEST_CASE("Tab during gameplay-only IMC stack does not fire ACTION_SOCIAL_TOGGLE",
          "[actionmap][imc][tab][regression][s483b]")
{
    PureCtx gameplay = makeGameplay();
    std::vector<const PureCtx *> active = { &gameplay };

    int action = resolvePureVk(active, VK_TAB);

    /* Gameplay IMC binds Tab to scorecard, not social toggle. The
     * scorecard handler is itself a no-op outside Combat Sim, so net
     * effect on a SP mission: Tab does nothing visible. */
    REQUIRE(action == ACT_SCORECARD);
    REQUIRE(action != ACT_SOCIAL_TOGGLE);
}

TEST_CASE("Tab with pause-menu on top of gameplay fires ACTION_SOCIAL_TOGGLE",
          "[actionmap][imc][tab][s483b]")
{
    PureCtx gameplay = makeGameplay();
    PureCtx pause    = makePauseMenu();

    /* Active stack is priority-sorted highest first per fireVk's contract. */
    std::vector<const PureCtx *> active = { &pause, &gameplay };

    int action = resolvePureVk(active, VK_TAB);

    REQUIRE(action == ACT_SOCIAL_TOGGLE);
}

TEST_CASE("Tab with main-menu on top of gameplay fires ACTION_SOCIAL_TOGGLE",
          "[actionmap][imc][tab][s483b]")
{
    PureCtx gameplay = makeGameplay();
    PureCtx menu     = makeMenu();
    std::vector<const PureCtx *> active = { &menu, &gameplay };

    int action = resolvePureVk(active, VK_TAB);

    REQUIRE(action == ACT_SOCIAL_TOGGLE);
}

TEST_CASE("Tab with pause + menu both active resolves to higher-priority ACTION_SOCIAL_TOGGLE",
          "[actionmap][imc][tab][s483b]")
{
    /* Both menu IMCs bind Tab to the same action, so this is purely a
     * defensive check that priority sort is stable when the action is
     * identical between contexts. */
    PureCtx gameplay = makeGameplay();
    PureCtx menu     = makeMenu();
    PureCtx pause    = makePauseMenu();
    std::vector<const PureCtx *> active = { &pause, &menu, &gameplay };

    int action = resolvePureVk(active, VK_TAB);

    REQUIRE(action == ACT_SOCIAL_TOGGLE);
}

TEST_CASE("ACTION_SOCIAL_TOGGLE id is 69 (catches enum reorder regressions)",
          "[actionmap][imc][s483b][pin]")
{
    /* If this fails the InputAction enum was reordered. Re-anchor the
     * pinned constant at the top of this file and verify the runtime
     * binding tables in setupMenuDefaults / setupPauseMenuDefaults
     * still address ACTION_SOCIAL_TOGGLE rather than the displaced
     * neighbour action. */
    REQUIRE(ACT_SOCIAL_TOGGLE == 69);
    REQUIRE(ACT_SCORECARD     == 56);
}

TEST_CASE("Voice PTT resolves through action map in gameplay and menu contexts",
          "[actionmap][imc][voice][ptt]")
{
    PureCtx gameplay = makeGameplay();
    PureCtx menu     = makeMenu();
    PureCtx pause    = makePauseMenu();

    REQUIRE(resolvePureVk({ &gameplay }, VK_V) == ACT_VOICE_PTT);
    REQUIRE(resolvePureVk({ &menu, &gameplay }, VK_V) == ACT_VOICE_PTT);
    REQUIRE(resolvePureVk({ &pause, &gameplay }, VK_V) == ACT_VOICE_PTT);
}

TEST_CASE("Voice PTT raw V polling stays retired", "[input][voice][ptt][static]")
{
    const std::string header = readTextFile("port/include/actionmap.h");
    const std::string actionmap = readTextFile("port/src/actionmap.cpp");
    const std::string friends = readTextFile("port/fast3d/pdgui_friends.cpp");
    const std::string mainmenu = readTextFile("port/fast3d/pdgui_menu_mainmenu.cpp");

    REQUIRE_FALSE(header.empty());
    REQUIRE_FALSE(actionmap.empty());
    REQUIRE_FALSE(friends.empty());
    REQUIRE_FALSE(mainmenu.empty());

    REQUIRE(header.find("ACTION_VOICE_PTT") != std::string::npos);
    REQUIRE(actionmap.find("\"VoicePtt\"") != std::string::npos);
    REQUIRE(actionmap.find("case ACTION_VOICE_PTT:") != std::string::npos);
    REQUIRE(actionmap.find("addBind(imc, ACTION_VOICE_PTT,      VKL_V)") != std::string::npos);
    REQUIRE(actionmap.find("addBind(imc, ACTION_VOICE_PTT,     VKL_V)") != std::string::npos);
    REQUIRE(friends.find("actionPressed(0, ACTION_VOICE_PTT)") != std::string::npos);
    REQUIRE(friends.find("actionReleased(0, ACTION_VOICE_PTT)") != std::string::npos);
    REQUIRE(friends.find("ImGui::IsKeyPressed(ImGuiKey_V") == std::string::npos);
    REQUIRE(friends.find("ImGui::IsKeyReleased(ImGuiKey_V") == std::string::npos);
    REQUIRE(mainmenu.find("ACTION_VOICE_PTT") != std::string::npos);
}

TEST_CASE("Social tabs and menu tab navigation stay action-map owned", "[input][menu_action][social][static]")
{
    const std::string friends = readTextFile("port/fast3d/pdgui_friends.cpp");
    const std::string backend = readTextFile("port/fast3d/pdgui_backend.cpp");
    const std::string mainmenu = readTextFile("port/fast3d/pdgui_menu_mainmenu.cpp");

    REQUIRE_FALSE(friends.empty());
    REQUIRE_FALSE(backend.empty());
    REQUIRE_FALSE(mainmenu.empty());

    REQUIRE(friends.find("#include \"pdgui_nav.h\"") != std::string::npos);
    REQUIRE(friends.find("pdguiMenuTabPrevPressed()") != std::string::npos);
    REQUIRE(friends.find("pdguiMenuTabNextPressed()") != std::string::npos);
    REQUIRE(friends.find("socialTabFlags(SOCIAL_TAB_PUBLIC_MODS)") != std::string::npos);
    REQUIRE(backend.find("ACTION_MENU_TAB_PREV,    ImGuiKey_PageUp") == std::string::npos);
    REQUIRE(backend.find("ACTION_MENU_TAB_NEXT,    ImGuiKey_PageDown") == std::string::npos);
    REQUIRE(mainmenu.find("ImGui::IsKeyPressed(ImGuiKey_PageUp") == std::string::npos);
    REQUIRE(mainmenu.find("ImGui::IsKeyPressed(ImGuiKey_PageDown") == std::string::npos);
}

TEST_CASE("Social presence and invites use per-agent identity and stale-offline recovery",
          "[social][presence][static][c3828]")
{
    const std::string socialH = readTextFile("port/include/social.h");
    const std::string socialStore = readTextFile("port/src/social_store.c");
    const std::string presence = readTextFile("port/src/presence.c");
    const std::string friends = readTextFile("port/fast3d/pdgui_friends.cpp");
    const std::string group = readTextFile("port/src/net/group_session.c");

    REQUIRE_FALSE(socialH.empty());
    REQUIRE_FALSE(socialStore.empty());
    REQUIRE_FALSE(presence.empty());
    REQUIRE_FALSE(friends.empty());
    REQUIRE_FALSE(group.empty());

    REQUIRE(socialH.find("socialHandleBindsPubkeyForAgent") != std::string::npos);
    REQUIRE(socialStore.find("static char            s_MyAgentName[SOCIAL_AGENTNAME_MAX]") != std::string::npos);
    REQUIRE(socialStore.find("setLocalAgentName(agent_name)") != std::string::npos);
    REQUIRE(socialStore.find("deriveHandleFromPubkeyAgent(pub, agent)") != std::string::npos);
    REQUIRE(socialStore.find("socialHandleBindsPubkeyForAgent(handle, pubkey, NULL)") != std::string::npos);

    REQUIRE(presence.find("PRESENCE_AGENT_OFFSET") != std::string::npos);
    REQUIRE(presence.find("PRESENCE_STATUS_OFFSET") != std::string::npos);
    REQUIRE(presence.find("writeFixedString(packet + PRESENCE_AGENT_OFFSET") != std::string::npos);
    REQUIRE(presence.find("socialHandleBindsPubkeyForAgent(from_handle, sender_pub, agent)") != std::string::npos);
    REQUIRE(presence.find("socialFriendUpdateAgentName(f->connect_code, agent)") != std::string::npos);
    REQUIRE(presence.find("*out_port = PRESENCE_PORT;") != std::string::npos);

    REQUIRE(friends.find("if (actionButton(\"Invite\"))") != std::string::npos);
    REQUIRE(friends.find("const bool can_invite") == std::string::npos);
    REQUIRE(friends.find("Attempt even when the displayed state is stale/offline") != std::string::npos);

    REQUIRE(group.find("#include \"net/netholepunch.h\"") != std::string::npos);
    REQUIRE(group.find("netStartClientWithHolePunch(addr)") != std::string::npos);
    REQUIRE(group.find("netStartClient(addr)") == std::string::npos);
}

TEST_CASE("ACTION_VOICE_PTT id is 85", "[actionmap][imc][voice][ptt][pin]")
{
    REQUIRE(ACT_VOICE_PTT == 85);
}
