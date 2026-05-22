/*
 * test_menu_graph.cpp -- source guards for the first menu graph slice.
 *
 * @SYNC port/include/menupool.h
 * @SYNC port/src/menupool.c
 * @SYNC port/fast3d/pdgui_menu_mainmenu.cpp
 */

#include "catch.hpp"

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

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

std::string functionBlock(const std::string &text, const std::string &name)
{
    const std::string needle = name + "(";
    size_t start = text.find(needle);
    if (start == std::string::npos) {
        return {};
    }
    size_t brace = text.find('{', start);
    if (brace == std::string::npos) {
        return {};
    }
    int depth = 0;
    for (size_t i = brace; i < text.size(); ++i) {
        if (text[i] == '{') {
            ++depth;
        } else if (text[i] == '}') {
            --depth;
            if (depth == 0) {
                return text.substr(start, i - start + 1);
            }
        }
    }
    return {};
}

std::vector<size_t> findAll(const std::string &text, const std::string &needle)
{
    std::vector<size_t> out;
    size_t pos = 0;
    while ((pos = text.find(needle, pos)) != std::string::npos) {
        out.push_back(pos);
        pos += needle.size();
    }
    return out;
}

void requireNoRawMenuShortcutPolling(const std::string &text)
{
    REQUIRE(text.find("ImGui::IsKeyPressed(ImGuiKey_Enter") == std::string::npos);
    REQUIRE(text.find("ImGui::IsKeyPressed(ImGuiKey_KeypadEnter") == std::string::npos);
    REQUIRE(text.find("ImGui::IsKeyPressed(ImGuiKey_Space") == std::string::npos);
    REQUIRE(text.find("ImGui::IsKeyPressed(ImGuiKey_Escape") == std::string::npos);
}

void requireNoRawMenuNavigationPolling(const std::string &text)
{
    REQUIRE(text.find("ImGui::IsKeyPressed(ImGuiKey_UpArrow") == std::string::npos);
    REQUIRE(text.find("ImGui::IsKeyPressed(ImGuiKey_DownArrow") == std::string::npos);
    REQUIRE(text.find("ImGui::IsKeyPressed(ImGuiKey_LeftArrow") == std::string::npos);
    REQUIRE(text.find("ImGui::IsKeyPressed(ImGuiKey_RightArrow") == std::string::npos);
    REQUIRE(text.find("ImGui::IsKeyPressed(ImGuiKey_PageUp") == std::string::npos);
    REQUIRE(text.find("ImGui::IsKeyPressed(ImGuiKey_PageDown") == std::string::npos);
    REQUIRE(text.find("ImGui::IsKeyPressed(ImGuiKey_Q") == std::string::npos);
    REQUIRE(text.find("ImGui::IsKeyPressed(ImGuiKey_E,") == std::string::npos);
}

void requireNoRawMenuCommandPolling(const std::string &text)
{
    REQUIRE(text.find("ImGui::IsKeyPressed(ImGuiKey_C") == std::string::npos);
    REQUIRE(text.find("ImGui::IsKeyPressed(ImGuiKey_D,") == std::string::npos);
    REQUIRE(text.find("ImGui::IsKeyPressed(ImGuiKey_Delete") == std::string::npos);
    REQUIRE(text.find("ImGui::IsKeyPressed(ImGuiKey_GamepadFaceLeft") == std::string::npos);
    REQUIRE(text.find("ImGui::IsKeyPressed(ImGuiKey_GamepadFaceUp") == std::string::npos);
}

} /* namespace */

TEST_CASE("menu graph: main menu subviews have dedicated pool identities", "[input][menu_graph][static]")
{
    const std::string header = readTextFile("port/include/menupool.h");
    const std::string pool = readTextFile("port/src/menupool.c");

    REQUIRE_FALSE(header.empty());
    REQUIRE_FALSE(pool.empty());

    REQUIRE(header.find("MENU_TYPE_MAIN_SOLO_VIEW") != std::string::npos);
    REQUIRE(header.find("MENU_TYPE_MAIN_SETTINGS_VIEW") != std::string::npos);
    REQUIRE(header.find("MENU_TYPE_MAIN_MODDING_VIEW") != std::string::npos);
    REQUIRE(header.find("MENU_TYPE_MAIN_STATS_VIEW") != std::string::npos);
    REQUIRE(header.find("MENU_TYPE_GRID_SUBMENU") != std::string::npos);
    REQUIRE(header.find("MENU_TYPE_NETWORK_JOINING") != std::string::npos);

    REQUIRE(pool.find("[MENU_TYPE_MAIN_SOLO_VIEW]      = \"main_solo_view\"") != std::string::npos);
    REQUIRE(pool.find("[MENU_TYPE_MAIN_SETTINGS_VIEW]  = \"main_settings_view\"") != std::string::npos);
    REQUIRE(pool.find("[MENU_TYPE_MAIN_MODDING_VIEW]   = \"main_modding_view\"") != std::string::npos);
    REQUIRE(pool.find("[MENU_TYPE_MAIN_STATS_VIEW]     = \"main_stats_view\"") != std::string::npos);
    REQUIRE(pool.find("[MENU_TYPE_NETWORK_JOINING]     = \"network_joining\"") != std::string::npos);
    REQUIRE(pool.find("REG(&g_NetJoiningDialog,             MENU_TYPE_NETWORK_JOINING)") != std::string::npos);
    REQUIRE(pool.find("REG(&g_FilemgrEnterNameMenuDialog,   MENU_TYPE_AGENT_CREATE)") != std::string::npos);
}

TEST_CASE("menu action helpers replace raw confirm-modal key polling", "[input][menu_action][static]")
{
    const std::string navHeader = readTextFile("port/include/pdgui_nav.h");
    const std::string navImpl = readTextFile("port/src/pdgui_nav.c");
    const std::string layout = readTextFile("port/fast3d/pdgui_layout.cpp");
    const std::string actionmap = readTextFile("port/src/actionmap.cpp");

    REQUIRE_FALSE(navHeader.empty());
    REQUIRE_FALSE(navImpl.empty());
    REQUIRE_FALSE(layout.empty());
    REQUIRE_FALSE(actionmap.empty());

    REQUIRE(navHeader.find("pdguiMenuAcceptPressed") != std::string::npos);
    REQUIRE(navHeader.find("pdguiMenuCancelPressed") != std::string::npos);
    REQUIRE(navHeader.find("pdguiMenuActionRepeat") != std::string::npos);
    REQUIRE(navHeader.find("pdguiMenuSecondaryPressed") != std::string::npos);
    REQUIRE(navHeader.find("pdguiMenuTertiaryPressed") != std::string::npos);
    REQUIRE(navHeader.find("pdguiMenuStartPressed") != std::string::npos);
    REQUIRE(navHeader.find("pdguiMenuDeletePressed") != std::string::npos);
    REQUIRE(navImpl.find("actionPressed(0, action)") != std::string::npos);
    REQUIRE(navImpl.find("actionHeld(0, action)") != std::string::npos);
    REQUIRE(navImpl.find("ACTION_MENU_SECONDARY") != std::string::npos);
    REQUIRE(navImpl.find("ACTION_MENU_TERTIARY") != std::string::npos);
    REQUIRE(navImpl.find("ACTION_PAUSE") != std::string::npos);
    REQUIRE(navImpl.find("ACTION_MENU_DELETE") != std::string::npos);

    const std::string menuDefaults = functionBlock(actionmap, "static void setupMenuDefaults");
    const std::string pauseDefaults = functionBlock(actionmap, "static void setupPauseMenuDefaults");
    REQUIRE(menuDefaults.find("addBind(imc, ACTION_USE,          VK_SPACE)") != std::string::npos);
    REQUIRE(menuDefaults.find("addBind(imc, ACTION_USE,          VKL_KP_ENTER)") != std::string::npos);
    REQUIRE(menuDefaults.find("addBind(imc, ACTION_MENU_TAB_PREV,VKL_PAGEUP)") != std::string::npos);
    REQUIRE(menuDefaults.find("addBind(imc, ACTION_MENU_TAB_PREV,VKL_Q)") != std::string::npos);
    REQUIRE(menuDefaults.find("addBind(imc, ACTION_MENU_TAB_NEXT,VKL_PAGEDOWN)") != std::string::npos);
    REQUIRE(menuDefaults.find("addBind(imc, ACTION_MENU_TAB_NEXT,VKL_E)") != std::string::npos);
    REQUIRE(menuDefaults.find("addBind(imc, ACTION_MENU_SECONDARY,VKL_C)") != std::string::npos);
    REQUIRE(menuDefaults.find("addBind(imc, ACTION_MENU_SECONDARY,JOY_BTN(0, JBTN_X))") != std::string::npos);
    REQUIRE(menuDefaults.find("addBind(imc, ACTION_MENU_TERTIARY,VKL_D)") != std::string::npos);
    REQUIRE(menuDefaults.find("addBind(imc, ACTION_MENU_TERTIARY,JOY_BTN(0, JBTN_Y))") != std::string::npos);
    REQUIRE(menuDefaults.find("addBind(imc, ACTION_MENU_DELETE,VK_DELETE)") != std::string::npos);
    REQUIRE(pauseDefaults.find("addBind(imc, ACTION_USE,          VK_SPACE)") != std::string::npos);
    REQUIRE(pauseDefaults.find("addBind(imc, ACTION_USE,          VKL_KP_ENTER)") != std::string::npos);
    REQUIRE(pauseDefaults.find("addBind(imc, ACTION_MENU_TAB_PREV,VKL_PAGEUP)") != std::string::npos);
    REQUIRE(pauseDefaults.find("addBind(imc, ACTION_MENU_TAB_PREV,VKL_Q)") != std::string::npos);
    REQUIRE(pauseDefaults.find("addBind(imc, ACTION_MENU_TAB_NEXT,VKL_PAGEDOWN)") != std::string::npos);
    REQUIRE(pauseDefaults.find("addBind(imc, ACTION_MENU_TAB_NEXT,VKL_E)") != std::string::npos);
    REQUIRE(pauseDefaults.find("addBind(imc, ACTION_MENU_SECONDARY,VKL_C)") != std::string::npos);
    REQUIRE(pauseDefaults.find("addBind(imc, ACTION_MENU_SECONDARY,JOY_BTN(0, JBTN_X))") != std::string::npos);
    REQUIRE(pauseDefaults.find("addBind(imc, ACTION_MENU_TERTIARY,VKL_D)") != std::string::npos);
    REQUIRE(pauseDefaults.find("addBind(imc, ACTION_MENU_TERTIARY,JOY_BTN(0, JBTN_Y))") != std::string::npos);
    REQUIRE(pauseDefaults.find("addBind(imc, ACTION_MENU_DELETE,VK_DELETE)") != std::string::npos);

    const std::string confirm = functionBlock(layout, "s32 pdguiRenderConfirmModal");
    const std::string actionBar = functionBlock(layout, "s32 pdguiActionBarButton");
    const std::string style = readTextFile("port/fast3d/pdgui_style.cpp");
    const std::string chrome = functionBlock(style, "pdguiDrawPdDialog");
    REQUIRE(confirm.find("pdguiMenuAcceptPressed()") != std::string::npos);
    REQUIRE(confirm.find("pdguiMenuCancelPressed()") != std::string::npos);
    requireNoRawMenuShortcutPolling(confirm);
    REQUIRE(actionBar.find("pdguiMenuAcceptPressed()") != std::string::npos);
    REQUIRE(chrome.find("ImGuiFocusedFlags_RootAndChildWindows") != std::string::npos);
}

TEST_CASE("menu action helpers replace priority navigation and tab polling", "[input][menu_action][static]")
{
    const std::string agent = readTextFile("port/fast3d/pdgui_menu_agentselect.cpp");
    const std::string mainmenu = readTextFile("port/fast3d/pdgui_menu_mainmenu.cpp");
    const std::string room = readTextFile("port/fast3d/pdgui_menu_room.cpp");
    const std::string stats = readTextFile("port/fast3d/pdgui_menu_stats.cpp");

    REQUIRE_FALSE(agent.empty());
    REQUIRE_FALSE(mainmenu.empty());
    REQUIRE_FALSE(room.empty());
    REQUIRE_FALSE(stats.empty());

    const std::string agentRender = functionBlock(agent, "renderAgentSelect");
    const std::string settings = functionBlock(mainmenu, "renderSettingsView");
    const std::string cinema = functionBlock(mainmenu, "renderCinemaList");
    const size_t roomRenderStart = room.find("extern \"C\" void pdguiRoomScreenRender");
    REQUIRE(roomRenderStart != std::string::npos);
    const std::string roomRender = functionBlock(room.substr(roomRenderStart), "pdguiRoomScreenRender");
    const std::string statsRender = functionBlock(stats, "pdguiMenuStatsRender");
    REQUIRE_FALSE(agentRender.empty());
    REQUIRE_FALSE(settings.empty());
    REQUIRE_FALSE(cinema.empty());
    REQUIRE_FALSE(roomRender.empty());
    REQUIRE_FALSE(statsRender.empty());

    REQUIRE(agent.find("#include \"pdgui_nav.h\"") != std::string::npos);
    REQUIRE(stats.find("#include \"pdgui_nav.h\"") != std::string::npos);

    REQUIRE(agentRender.find("pdguiMenuAcceptPressed()") != std::string::npos);
    REQUIRE(agentRender.find("pdguiMenuCancelPressed()") != std::string::npos);
    REQUIRE(agentRender.find("pdguiMenuDownRepeat()") != std::string::npos);
    REQUIRE(agentRender.find("pdguiMenuUpRepeat()") != std::string::npos);
    requireNoRawMenuShortcutPolling(agentRender);
    requireNoRawMenuNavigationPolling(agentRender);

    REQUIRE(settings.find("pdguiMenuTabPrevPressed()") != std::string::npos);
    REQUIRE(settings.find("pdguiMenuTabNextPressed()") != std::string::npos);
    requireNoRawMenuNavigationPolling(settings);

    REQUIRE(cinema.find("pdguiMenuCancelPressed()") != std::string::npos);
    REQUIRE(cinema.find("pdguiMenuAcceptPressed()") != std::string::npos);
    REQUIRE(cinema.find("pdguiMenuDownRepeat()") != std::string::npos);
    REQUIRE(cinema.find("pdguiMenuUpRepeat()") != std::string::npos);
    requireNoRawMenuShortcutPolling(cinema);
    requireNoRawMenuNavigationPolling(cinema);

    REQUIRE(roomRender.find("pdguiMenuTabPrevPressed()") != std::string::npos);
    REQUIRE(roomRender.find("pdguiMenuTabNextPressed()") != std::string::npos);
    requireNoRawMenuNavigationPolling(roomRender);

    REQUIRE(statsRender.find("pdguiMenuTabPrevPressed()") != std::string::npos);
    REQUIRE(statsRender.find("pdguiMenuTabNextPressed()") != std::string::npos);
    REQUIRE(statsRender.find("pdguiMenuCancelPressed()") != std::string::npos);
    requireNoRawMenuShortcutPolling(statsRender);
    requireNoRawMenuNavigationPolling(statsRender);
}

TEST_CASE("menu action helpers replace simple legacy menu back polling", "[input][menu_action][static]")
{
    const char *paths[] = {
        "port/fast3d/pdgui_countdown.cpp",
        "port/fast3d/pdgui_filebrowser.cpp",
        "port/fast3d/pdgui_menu_agentcreate.cpp",
        "port/fast3d/pdgui_menu_challenges.cpp",
        "port/fast3d/pdgui_menu_controldiagram.cpp",
        "port/fast3d/pdgui_menu_mpadvanced.cpp",
        "port/fast3d/pdgui_menu_mpsettings.cpp",
        "port/fast3d/pdgui_menu_mpsetup.cpp",
        "port/fast3d/pdgui_menu_playerconfig.cpp",
        "port/fast3d/pdgui_menu_teamsetup.cpp",
    };

    for (const char *path : paths) {
        const std::string source = readTextFile(path);
        REQUIRE_FALSE(source.empty());
        REQUIRE(source.find("#include \"pdgui_nav.h\"") != std::string::npos);
        requireNoRawMenuShortcutPolling(source);
        requireNoRawMenuNavigationPolling(source);
    }
}

TEST_CASE("menu action helpers replace cheats and modding panel polling", "[input][menu_action][static]")
{
    const char *paths[] = {
        "port/fast3d/pdgui_menu_cheats.cpp",
        "port/fast3d/pdgui_menu_modmgr.cpp",
        "port/fast3d/pdgui_menu_moddinghub.cpp",
    };

    for (const char *path : paths) {
        const std::string source = readTextFile(path);
        REQUIRE_FALSE(source.empty());
        REQUIRE(source.find("#include \"pdgui_nav.h\"") != std::string::npos);
        requireNoRawMenuShortcutPolling(source);
        requireNoRawMenuNavigationPolling(source);
    }
}

TEST_CASE("menu action helpers replace training menu polling", "[input][menu_action][static]")
{
    const std::string source = readTextFile("port/fast3d/pdgui_menu_training.cpp");

    REQUIRE_FALSE(source.empty());
    REQUIRE(source.find("#include \"pdgui_nav.h\"") != std::string::npos);
    requireNoRawMenuShortcutPolling(source);
    requireNoRawMenuNavigationPolling(source);
}

TEST_CASE("menu action helpers replace Solo Mission menu polling", "[input][menu_action][static]")
{
    const std::string source = readTextFile("port/fast3d/pdgui_menu_solomission.cpp");

    REQUIRE_FALSE(source.empty());
    REQUIRE(source.find("#include \"pdgui_nav.h\"") != std::string::npos);
    requireNoRawMenuShortcutPolling(source);
    requireNoRawMenuNavigationPolling(source);
}

TEST_CASE("menu action helpers replace secondary menu command polling", "[input][menu_action][static]")
{
    const std::string agent = readTextFile("port/fast3d/pdgui_menu_agentselect.cpp");
    const std::string room = readTextFile("port/fast3d/pdgui_menu_room.cpp");
    const std::string mpsettings = readTextFile("port/fast3d/pdgui_menu_mpsettings.cpp");

    REQUIRE_FALSE(agent.empty());
    REQUIRE_FALSE(room.empty());
    REQUIRE_FALSE(mpsettings.empty());

    REQUIRE(agent.find("pdguiMenuSecondaryPressed()") != std::string::npos);
    REQUIRE(agent.find("pdguiMenuTertiaryPressed()") != std::string::npos);
    REQUIRE(agent.find("pdguiMenuDeletePressed()") != std::string::npos);
    REQUIRE(room.find("pdguiMenuSecondaryPressed()") != std::string::npos);
    REQUIRE(room.find("pdguiMenuTertiaryPressed()") == std::string::npos);
    REQUIRE(room.find("pdguiMenuStartPressed()") != std::string::npos);
    REQUIRE(mpsettings.find("pdguiMenuSecondaryPressed()") != std::string::npos);

    requireNoRawMenuCommandPolling(agent);
    requireNoRawMenuCommandPolling(room);
    requireNoRawMenuCommandPolling(mpsettings);
}

TEST_CASE("menu graph: main menu view changes go through subview pool helper", "[input][menu_graph][static]")
{
    const std::string source = readTextFile("port/fast3d/pdgui_menu_mainmenu.cpp");

    REQUIRE_FALSE(source.empty());

    const std::string map = functionBlock(source, "pdguiMainMenuViewPoolType");
    const std::string set = functionBlock(source, "pdguiMainMenuSetView");
    const std::string graphSubview = functionBlock(source, "pdguiMainMenuFireSubviewEdge");
    const std::string graphBack = functionBlock(source, "pdguiMainMenuFireSubviewBackEdge");
    const std::string graphGridEnter = functionBlock(source, "pdguiMainMenuGraphEnterGrid");
    const std::string gridSubmenu = functionBlock(source, "renderGridSubmenu");
    REQUIRE_FALSE(map.empty());
    REQUIRE_FALSE(set.empty());
    REQUIRE_FALSE(graphSubview.empty());
    REQUIRE_FALSE(graphBack.empty());
    REQUIRE_FALSE(graphGridEnter.empty());
    REQUIRE_FALSE(gridSubmenu.empty());

    REQUIRE(map.find("case 1: return MENU_TYPE_MAIN_SOLO_VIEW;") != std::string::npos);
    REQUIRE(map.find("case 2: return MENU_TYPE_MAIN_SETTINGS_VIEW;") != std::string::npos);
    REQUIRE(map.find("case 3: return MENU_TYPE_MAIN_MODDING_VIEW;") != std::string::npos);
    REQUIRE(map.find("case 5: return MENU_TYPE_MAIN_STATS_VIEW;") != std::string::npos);
    REQUIRE(map.find("case 6: return MENU_TYPE_GRID_SUBMENU;") != std::string::npos);
    REQUIRE(set.find("view == 4") != std::string::npos);

    REQUIRE(set.find("menupoolRelease(oldType)") != std::string::npos);
    REQUIRE(set.find("menupoolAcquire(newType, NULL, NULL)") != std::string::npos);
    REQUIRE(set.find("MENU_GRAPH: main menu view") != std::string::npos);

    REQUIRE(graphSubview.find("menuGraphEdge(MENU_TYPE_MAIN_MENU, edge_id)") != std::string::npos);
    REQUIRE(graphSubview.find("edge->kind != MENU_GRAPH_DEST_PUSH_MENU") != std::string::npos);
    REQUIRE(graphSubview.find("edge->payload.push_target != target") != std::string::npos);
    REQUIRE(graphSubview.find("pdguiMainMenuSetView(view, reason)") != std::string::npos);
    REQUIRE(graphBack.find("menuGraphEdge(source, \"back\")") != std::string::npos);
    REQUIRE(graphBack.find("edge->kind != MENU_GRAPH_DEST_POP_TO_PARENT") != std::string::npos);
    REQUIRE(graphBack.find("pdguiMainMenuSetView(0, reason)") != std::string::npos);
    REQUIRE(graphGridEnter.find("gridCommitEnter() ? 0 : -1") != std::string::npos);
    REQUIRE(gridSubmenu.find("menuGraphFireSceneOp(MENU_TYPE_GRID_SUBMENU, \"enter\"") != std::string::npos);
    REQUIRE(gridSubmenu.find("gridCommitEnter()") == std::string::npos);

    REQUIRE(source.find("pdguiMainMenuFireSubviewEdge(\"solo_play\", 1, \"open-solo\")") != std::string::npos);
    REQUIRE(source.find("pdguiMainMenuFireSubviewEdge(\"settings\", 2, \"open-settings\")") != std::string::npos);
    REQUIRE(source.find("pdguiMainMenuFireSubviewEdge(\"modding\", 3, \"open-modding\")") != std::string::npos);
    REQUIRE(source.find("pdguiMainMenuFireSubviewEdge(\"online_play\", 4, \"open-online\")") == std::string::npos);
    REQUIRE(source.find("pdguiMainMenuFireSubviewEdge(\"stats\", 5, \"open-stats\")") != std::string::npos);
    REQUIRE(source.find("pdguiMainMenuFireSubviewEdge(\"grid\", 6, \"open-grid\")") != std::string::npos);
    REQUIRE(source.find("pdguiMainMenuSetView(1, \"open-solo\")") == std::string::npos);
    REQUIRE(source.find("pdguiMainMenuSetView(2, \"open-settings\")") == std::string::npos);
    REQUIRE(source.find("pdguiMainMenuSetView(3, \"open-modding\")") == std::string::npos);
    REQUIRE(source.find("pdguiMainMenuSetView(5, \"open-stats\")") == std::string::npos);
    REQUIRE(source.find("pdguiMainMenuSetView(6, \"open-grid\")") == std::string::npos);
    REQUIRE(source.find("pdguiMainMenuFireSubviewBackEdge(\"close-subview\")") != std::string::npos);
    REQUIRE(source.find("pdguiMainMenuFireSubviewBackEdge(\"grid-back\")") != std::string::npos);
    REQUIRE(source.find("pdguiMainMenuFireSubviewBackEdge(\"modding-back\")") != std::string::npos);
    REQUIRE(source.find("pdguiMainMenuFireSubviewBackEdge(\"stats-closed\")") != std::string::npos);
    REQUIRE(source.find("pdguiMainMenuSetView(0, \"close-subview\")") == std::string::npos);
    REQUIRE(source.find("pdguiMainMenuSetView(0, \"grid-back\")") == std::string::npos);
    REQUIRE(source.find("pdguiMainMenuSetView(0, \"modding-back\")") == std::string::npos);
    REQUIRE(source.find("pdguiMainMenuSetView(0, \"stats-closed\")") == std::string::npos);
    REQUIRE(source.find("pdguiMainMenuSetView(0, \"external-reset\")") != std::string::npos);
    REQUIRE(source.find("pdguiMainMenuSetView(s_MenuView, \"render-sync\")") != std::string::npos);

    REQUIRE(source.find("s_PrevView != 6 && s_MenuView == 6") == std::string::npos);

    const std::vector<size_t> assignments = findAll(source, "s_MenuView = ");
    REQUIRE(assignments.size() == 2);
    REQUIRE(source.find("static s32 s_MenuView = 0;") != std::string::npos);
    REQUIRE(set.find("s_MenuView = view;") != std::string::npos);
}

TEST_CASE("menu graph: main-menu Solo view uses graph push ops", "[input][menu_graph][mainmenu][static]")
{
    const std::string mainmenu = readTextFile("port/fast3d/pdgui_menu_mainmenu.cpp");
    const std::string graph = readTextFile("port/src/menugraph.c");

    REQUIRE_FALSE(mainmenu.empty());
    REQUIRE_FALSE(graph.empty());
    REQUIRE(graph.find("EDGE_PUSH(\"solo_missions\", ACTION_MENU_ACCEPT, \"Solo Missions\", MENU_TYPE_SOLO_MISSION)") != std::string::npos);
    REQUIRE(graph.find("EDGE_PUSH(\"combat_simulator\", ACTION_MENU_ACCEPT, \"Combat Simulator\", MENU_TYPE_ROOM)") != std::string::npos);

    const std::string solo = functionBlock(mainmenu, "pdguiMainMenuGraphSoloMissions");
    const std::string cs = functionBlock(mainmenu, "pdguiMainMenuGraphCombatSimulator");
    const std::string render = functionBlock(mainmenu, "renderMainMenu");
    REQUIRE_FALSE(solo.empty());
    REQUIRE_FALSE(cs.empty());
    REQUIRE_FALSE(render.empty());

    REQUIRE(solo.find("pdguiSoloMissionReset()") != std::string::npos);
    REQUIRE(solo.find("menuhandlerMainMenuSoloMissions(MENUOP_SET") != std::string::npos);
    REQUIRE(cs.find("menuhandlerMainMenuCombatSimulator(MENUOP_SET") != std::string::npos);
    REQUIRE(render.find("menuGraphFirePushOp(MENU_TYPE_MAIN_SOLO_VIEW, \"solo_missions\"") != std::string::npos);
    REQUIRE(render.find("menuGraphFirePushOp(MENU_TYPE_MAIN_SOLO_VIEW, \"combat_simulator\"") != std::string::npos);
    REQUIRE(render.find("menuhandlerMainMenuSoloMissions(MENUOP_SET") == std::string::npos);
    REQUIRE(render.find("menuhandlerMainMenuCombatSimulator(MENUOP_SET") == std::string::npos);
    REQUIRE(render.find("pdguiSoloRoomOpen();") == std::string::npos);
}

TEST_CASE("menu pool: watchdog preserves standalone Combat Simulator room", "[input][menupool][static][b351]")
{
    const std::string header = readTextFile("port/include/menupool.h");
    const std::string pool = readTextFile("port/src/menupool.c");
    const std::string menu = readTextFile("src/game/menu.c");
    const std::string room = readTextFile("port/fast3d/pdgui_menu_room.cpp");

    REQUIRE_FALSE(header.empty());
    REQUIRE_FALSE(pool.empty());
    REQUIRE_FALSE(menu.empty());
    REQUIRE_FALSE(room.empty());

    REQUIRE(header.find("menupoolCountLegacyStackLeaks") != std::string::npos);
    REQUIRE(header.find("menupoolDumpLegacyStackLeaks") != std::string::npos);
    REQUIRE(header.find("menupoolReleaseLegacyStackLeaks") != std::string::npos);

    const std::string optional = functionBlock(pool, "menupoolLegacyStackOptional");
    const std::string watchdog = functionBlock(menu, "menuPoolConsistencyCheck");
    const size_t roomRenderStart = room.find("extern \"C\" void pdguiRoomScreenRender");
    REQUIRE_FALSE(optional.empty());
    REQUIRE_FALSE(watchdog.empty());
    REQUIRE(roomRenderStart != std::string::npos);

    const std::string roomRender = functionBlock(room.substr(roomRenderStart), "pdguiRoomScreenRender");
    REQUIRE_FALSE(roomRender.empty());

    REQUIRE(optional.find("case MENU_TYPE_ROOM:") != std::string::npos);
    REQUIRE(optional.find("case MENU_TYPE_PAUSE_MENU:") != std::string::npos);
    REQUIRE(optional.find("case MENU_TYPE_SOCIAL_LOBBY:") != std::string::npos);
    REQUIRE(optional.find("case MENU_TYPE_SOCIAL_SHELL:") != std::string::npos);
    REQUIRE(roomRender.find("menupoolAcquire(MENU_TYPE_ROOM, NULL, &g_CtxImGuiMenu)") != std::string::npos);

    REQUIRE(watchdog.find("menupoolCountLegacyStackLeaks()") != std::string::npos);
    REQUIRE(watchdog.find("menupoolDumpLegacyStackLeaks()") != std::string::npos);
    REQUIRE(watchdog.find("menupoolReleaseLegacyStackLeaks()") != std::string::npos);
    REQUIRE(watchdog.find("menupoolCountActive()") == std::string::npos);
    REQUIRE(watchdog.find("menupoolReleaseAll()") == std::string::npos);
}

TEST_CASE("menu graph: MP endscreen is not hidden by legacy save-player prompt", "[input][menu_graph][mp_endscreen][static][b356]")
{
    const std::string ingame = readTextFile("src/game/mplayer/ingame.c");
    const std::string mpingame = readTextFile("port/fast3d/pdgui_menu_mpingame.cpp");
    const std::string endscreen = readTextFile("port/fast3d/pdgui_menu_endscreen.cpp");
    const std::string bridge = readTextFile("port/fast3d/pdgui_bridge.c");

    REQUIRE_FALSE(ingame.empty());
    REQUIRE_FALSE(mpingame.empty());
    REQUIRE_FALSE(endscreen.empty());
    REQUIRE_FALSE(bridge.empty());

    const std::string pushEndscreen = functionBlock(ingame, "mpPushEndscreenDialog");
    REQUIRE_FALSE(pushEndscreen.empty());

    REQUIRE(pushEndscreen.find("menuPushRootDialog(&g_MpEndscreenIndGameOverMenuDialog, MENUROOT_MPENDSCREEN)") != std::string::npos);
    REQUIRE(pushEndscreen.find("menuPushRootDialog(&g_MpEndscreenTeamGameOverMenuDialog, MENUROOT_MPENDSCREEN)") != std::string::npos);
    REQUIRE(pushEndscreen.find("OPTION_ASKEDSAVEPLAYER") != std::string::npos);
    REQUIRE(ingame.find("g_MpEndscreenSavePlayerMenuDialog") != std::string::npos);
    REQUIRE(pushEndscreen.find("menuPushDialog(&g_MpEndscreenSavePlayerMenuDialog)") == std::string::npos);
    REQUIRE(pushEndscreen.find("hiding") != std::string::npos);
    REQUIRE(pushEndscreen.find("post-match screen") != std::string::npos);

    REQUIRE(mpingame.find("pdguiGameOverRender() (in pdgui_menu_pausemenu.cpp) owns the full tabbed") != std::string::npos);
    REQUIRE(mpingame.find("pdguiHotswapRegister(&g_MpEndscreenSavePlayerMenuDialog") != std::string::npos);
    REQUIRE(endscreen.find("pdguiEndscreenExitToRoom") != std::string::npos);
    REQUIRE(bridge.find("configSave(\"pd.ini\")") != std::string::npos);
}

TEST_CASE("menu graph: main-menu Modding hub uses graph push op", "[input][menu_graph][mainmenu][static]")
{
    const std::string mainmenu = readTextFile("port/fast3d/pdgui_menu_mainmenu.cpp");
    const std::string graph = readTextFile("port/src/menugraph.c");

    REQUIRE_FALSE(mainmenu.empty());
    REQUIRE_FALSE(graph.empty());
    REQUIRE(graph.find("EDGE_PUSH(\"open_hub\", ACTION_MENU_ACCEPT, \"Open Modding Hub\", MENU_TYPE_MODDING_HUB)") != std::string::npos);

    const std::string open = functionBlock(mainmenu, "pdguiMainMenuGraphOpenModdingHub");
    const std::string render = functionBlock(mainmenu, "renderMainMenu");
    REQUIRE_FALSE(open.empty());
    REQUIRE_FALSE(render.empty());

    REQUIRE(open.find("pdguiModdingHubShow()") != std::string::npos);
    REQUIRE(render.find("menuGraphFirePushOp(MENU_TYPE_MAIN_MODDING_VIEW, \"open_hub\"") != std::string::npos);
    REQUIRE(render.find("pdguiModdingHubShow()") == std::string::npos);
}

TEST_CASE("menu graph: main-menu Stats panel uses graph push op", "[input][menu_graph][mainmenu][static]")
{
    const std::string mainmenu = readTextFile("port/fast3d/pdgui_menu_mainmenu.cpp");
    const std::string graph = readTextFile("port/src/menugraph.c");

    REQUIRE_FALSE(mainmenu.empty());
    REQUIRE_FALSE(graph.empty());
    REQUIRE(graph.find("EDGE_PUSH(\"open_panel\", ACTION_MENU_ACCEPT, \"Stats Panel\", MENU_TYPE_STATS_PANEL)") != std::string::npos);

    const std::string open = functionBlock(mainmenu, "pdguiMainMenuGraphOpenStatsPanel");
    const std::string render = functionBlock(mainmenu, "renderMainMenu");
    REQUIRE_FALSE(open.empty());
    REQUIRE_FALSE(render.empty());

    REQUIRE(open.find("pdguiMenuStatsShow()") != std::string::npos);
    REQUIRE(render.find("menuGraphFirePushOp(MENU_TYPE_MAIN_STATS_VIEW, \"open_panel\"") != std::string::npos);
    REQUIRE(render.find("pdguiMenuStatsShow()") == std::string::npos);
}

TEST_CASE("menu graph: main-menu Quit uses process graph edge", "[input][menu_graph][mainmenu][static]")
{
    const std::string mainmenu = readTextFile("port/fast3d/pdgui_menu_mainmenu.cpp");
    const std::string graph = readTextFile("port/src/menugraph.c");

    REQUIRE_FALSE(mainmenu.empty());
    REQUIRE_FALSE(graph.empty());
    REQUIRE(graph.find("EDGE_PROCESS(\"quit\", ACTION_MENU_ACCEPT, \"Quit Game\")") != std::string::npos);

    const std::string quit = functionBlock(mainmenu, "pdguiMainMenuGraphQuit");
    const std::string render = functionBlock(mainmenu, "renderMainMenu");
    REQUIRE_FALSE(quit.empty());
    REQUIRE_FALSE(render.empty());

    REQUIRE(quit.find("SDL_QUIT") != std::string::npos);
    REQUIRE(quit.find("SDL_PushEvent(&quitEvent)") != std::string::npos);
    REQUIRE(render.find("menuGraphFireProcessOp(MENU_TYPE_MAIN_MENU, \"quit\"") != std::string::npos);
    REQUIRE(render.find("SDL_PushEvent(&quitEvent)") == std::string::npos);
}

TEST_CASE("menu graph: main-menu close uses pop graph edge", "[input][menu_graph][mainmenu][static]")
{
    const std::string mainmenu = readTextFile("port/fast3d/pdgui_menu_mainmenu.cpp");
    const std::string graph = readTextFile("port/src/menugraph.c");

    REQUIRE_FALSE(mainmenu.empty());
    REQUIRE_FALSE(graph.empty());
    REQUIRE(graph.find("EDGE_POP_ROOT(\"close\", ACTION_MENU_CANCEL, \"Close Main Menu\")") != std::string::npos);

    const std::string close = functionBlock(mainmenu, "pdguiMainMenuGraphClose");
    const std::string render = functionBlock(mainmenu, "renderMainMenu");
    REQUIRE_FALSE(close.empty());
    REQUIRE_FALSE(render.empty());

    REQUIRE(close.find("lvSetPaused(false)") != std::string::npos);
    REQUIRE(close.find("playerUnpause()") != std::string::npos);
    REQUIRE(close.find("g_PlayersWithControl[0] = true") != std::string::npos);
    REQUIRE(close.find("menuPopDialog()") == std::string::npos);
    REQUIRE(close.find("inputCtxPopDeferred(&g_CtxImGuiMenu)") == std::string::npos);
    REQUIRE(render.find("menuGraphFirePopOp(MENU_TYPE_MAIN_MENU, \"close\"") != std::string::npos);
    REQUIRE(render.find("menuPopDialog();") == std::string::npos);
    REQUIRE(render.find("inputCtxPopDeferred(&g_CtxImGuiMenu);") == std::string::npos);

    const std::string popOp = functionBlock(graph, "s32 menuGraphFirePopOp");
    REQUIRE_FALSE(popOp.empty());
    REQUIRE(popOp.find("if (rc == 0)") != std::string::npos);
    REQUIRE(popOp.find("menuPopDialog();") != std::string::npos);
    REQUIRE(popOp.find("menupoolReleaseAll();") != std::string::npos);
    REQUIRE(popOp.find("menuClose();") != std::string::npos);
}

TEST_CASE("menu graph: priority nodes and dialog-push firing substrate exist", "[input][menu_graph][static]")
{
    const std::string header = readTextFile("port/include/menugraph.h");
    const std::string graph = readTextFile("port/src/menugraph.c");
    const std::string mainmenu = readTextFile("port/fast3d/pdgui_menu_mainmenu.cpp");

    REQUIRE_FALSE(header.empty());
    REQUIRE_FALSE(graph.empty());
    REQUIRE_FALSE(mainmenu.empty());

    REQUIRE(header.find("typedef enum MenuGraphDestKind") != std::string::npos);
    REQUIRE(header.find("MENU_GRAPH_DEST_PUSH_MENU") != std::string::npos);
    REQUIRE(header.find("MENU_GRAPH_DEST_SWITCH_SIBLING") != std::string::npos);
    REQUIRE(header.find("MENU_GRAPH_DEST_SCENE_EVENT") != std::string::npos);
    REQUIRE(header.find("MENU_GRAPH_DEST_LOCAL_OP") != std::string::npos);
    REQUIRE(header.find("typedef struct MenuGraphNode") != std::string::npos);
    REQUIRE(header.find("menuGraphFirePushDialog") != std::string::npos);
    REQUIRE(header.find("menuGraphFireReplaceDialog") != std::string::npos);
    REQUIRE(header.find("menuGraphFirePushOp") != std::string::npos);
    REQUIRE(header.find("menuGraphFireSwitchSibling") != std::string::npos);
    REQUIRE(header.find("menuGraphFireSceneOp") != std::string::npos);
    REQUIRE(header.find("menuGraphFireNetworkOp") != std::string::npos);
    REQUIRE(header.find("menuGraphFireProcessOp") != std::string::npos);
    REQUIRE(header.find("menuGraphFirePopOp") != std::string::npos);
    REQUIRE(header.find("menuGraphFireLocalOp") != std::string::npos);
    REQUIRE(header.find("menuGraphFirePop") != std::string::npos);

    REQUIRE(graph.find("static const MenuGraphNode s_Nodes[]") != std::string::npos);
    REQUIRE(graph.find("NODE(MENU_TYPE_MAIN_MENU") != std::string::npos);
    REQUIRE(graph.find("NODE(MENU_TYPE_SOLO_MISSION") != std::string::npos);
    REQUIRE(graph.find("NODE(MENU_TYPE_ROOM") != std::string::npos);
    REQUIRE(graph.find("NODE(MENU_TYPE_ENDSCREEN_SOLO") != std::string::npos);
    REQUIRE(graph.find("NODE(MENU_TYPE_ENDSCREEN_MP") != std::string::npos);
    REQUIRE(graph.find("NODE(MENU_TYPE_PAUSE_MENU") != std::string::npos);
    REQUIRE(graph.find("NODE(MENU_TYPE_MP_PAUSE") != std::string::npos);
    REQUIRE(graph.find("NODE(MENU_TYPE_SOLO_MISSION_PAUSE") != std::string::npos);
    REQUIRE(graph.find("NODE(MENU_TYPE_SOLO_INVENTORY") != std::string::npos);
    REQUIRE(graph.find("NODE(MENU_TYPE_SOLO_OPTIONS") != std::string::npos);
    REQUIRE(graph.find("NODE(MENU_TYPE_SOCIAL_LOBBY") != std::string::npos);
    REQUIRE(graph.find("NODE(MENU_TYPE_NETWORK") != std::string::npos);
    REQUIRE(graph.find("NODE(MENU_TYPE_AGENT_SELECT") != std::string::npos);
    REQUIRE(graph.find("NODE(MENU_TYPE_CHEATS") != std::string::npos);
    REQUIRE(graph.find("NODE(MENU_TYPE_MP_SETTINGS") != std::string::npos);
    REQUIRE(graph.find("NODE(MENU_TYPE_TRAINING") != std::string::npos);
    REQUIRE(graph.find("NODE(MENU_TYPE_FR_WEAPON_LIST") != std::string::npos);
    REQUIRE(graph.find("NODE(MENU_TYPE_DT_LIST") != std::string::npos);
    REQUIRE(graph.find("NODE(MENU_TYPE_HT_LIST") != std::string::npos);
    REQUIRE(graph.find("NODE(MENU_TYPE_WARNING_MODAL") != std::string::npos);
    REQUIRE(graph.find("#define EDGE_PUSH_ANY") != std::string::npos);
    REQUIRE(graph.find("EDGE_PUSH_ANY(\"pd_mode_settings\", ACTION_MENU_ACCEPT, \"PD Mode Settings\")") != std::string::npos);
    REQUIRE(graph.find("EDGE_PUSH_ANY(\"bio_profile\", ACTION_MENU_ACCEPT, \"Bio Profile\")") != std::string::npos);
    REQUIRE(graph.find("EDGE_PUSH(\"select_music\", ACTION_MENU_ACCEPT, \"Select Music\", MENU_TYPE_MP_TUNES)") != std::string::npos);
    REQUIRE(graph.find("EDGE_PUSH_ANY(\"more_options\", ACTION_MENU_ACCEPT, \"More Options\")") != std::string::npos);
    REQUIRE(graph.find("EDGE_NETWORK(\"disconnect\", ACTION_MENU_ACCEPT") != std::string::npos);
    REQUIRE(graph.find("EDGE_PUSH(\"joining\", ACTION_MENU_ACCEPT, \"Joining\", MENU_TYPE_NETWORK_JOINING)") != std::string::npos);
    REQUIRE(graph.find("EDGE_POP(\"host_started\", ACTION_MENU_ACCEPT") != std::string::npos);
    REQUIRE(graph.find("EDGE_PUSH(\"team_setup\", ACTION_MENU_ACCEPT, \"Team Setup\", MENU_TYPE_MP_TEAM_SETUP)") != std::string::npos);
    REQUIRE(graph.find("EDGE_PUSH(\"select_music\", ACTION_MENU_ACCEPT, \"Select Music\", MENU_TYPE_MP_TUNES)") != std::string::npos);
    REQUIRE(graph.find("EDGE_NETWORK(\"leave_room\", ACTION_MENU_CANCEL, \"Leave Room\", \"leave_room\")") != std::string::npos);
    REQUIRE(graph.find("EDGE_PUSH(\"end_game\", ACTION_MENU_ACCEPT, \"End Game\", MENU_TYPE_WARNING_MODAL)") != std::string::npos);
    REQUIRE(graph.find("static const MenuGraphEdge s_SoloMissionPauseEdges[]") != std::string::npos);
    REQUIRE(graph.find("EDGE_SWITCH(\"inventory\", ACTION_MENU_ACCEPT, \"Inventory\", MENU_TYPE_SOLO_INVENTORY)") != std::string::npos);
    REQUIRE(graph.find("EDGE_SWITCH(\"settings\", ACTION_MENU_ACCEPT, \"Settings\", MENU_TYPE_SOLO_OPTIONS)") != std::string::npos);
    REQUIRE(graph.find("EDGE_PUSH(\"abort\", ACTION_MENU_ACCEPT, \"Abort Mission\", MENU_TYPE_WARNING_MODAL)") != std::string::npos);

    const std::string fire = functionBlock(graph, "menuGraphFirePushDialog");
    REQUIRE_FALSE(fire.empty());
    REQUIRE(fire.find("menupoolTypeForDialogdef(dialogdef)") != std::string::npos);
    REQUIRE(fire.find("edge->payload.push_target != MENU_TYPE_NONE") != std::string::npos);
    REQUIRE(fire.find("actual != edge->payload.push_target") != std::string::npos);
    REQUIRE(fire.find("MENU.GRAPH.FIRE") != std::string::npos);
    REQUIRE(fire.find("menuPushDialog(dialogdef)") != std::string::npos);

    const std::string replace = functionBlock(graph, "menuGraphFireReplaceDialog");
    REQUIRE_FALSE(replace.empty());
    REQUIRE(replace.find("menuPopDialog();") != std::string::npos);
    REQUIRE(replace.find("menuPushDialog(dialogdef);") != std::string::npos);

    const std::string pushOp = functionBlock(graph, "menuGraphFirePushOp");
    REQUIRE_FALSE(pushOp.empty());
    REQUIRE(pushOp.find("edge->kind != MENU_GRAPH_DEST_PUSH_MENU") != std::string::npos);
    REQUIRE(pushOp.find("edge->payload.push_target") != std::string::npos);
    REQUIRE(pushOp.find("MENU.GRAPH.RESULT") != std::string::npos);

    const std::string switchSibling = functionBlock(graph, "menuGraphFireSwitchSibling");
    REQUIRE_FALSE(switchSibling.empty());
    REQUIRE(switchSibling.find("menupoolTypeForDialogdef(dialogdef)") != std::string::npos);
    REQUIRE(switchSibling.find("actual != edge->payload.sibling_target") != std::string::npos);
    REQUIRE(switchSibling.find("menuSwitchToDialog(dialogdef)") != std::string::npos);

    const std::string sceneOp = functionBlock(graph, "menuGraphFireSceneOp");
    REQUIRE_FALSE(sceneOp.empty());
    REQUIRE(sceneOp.find("edge->kind != MENU_GRAPH_DEST_SCENE_EVENT") != std::string::npos);
    REQUIRE(sceneOp.find("edge->payload.scene_event") != std::string::npos);
    REQUIRE(sceneOp.find("MENU.GRAPH.RESULT") != std::string::npos);

    const std::string processOp = functionBlock(graph, "menuGraphFireProcessOp");
    REQUIRE_FALSE(processOp.empty());
    REQUIRE(processOp.find("edge->kind != MENU_GRAPH_DEST_PROCESS_EXIT") != std::string::npos);
    REQUIRE(processOp.find("MENU.GRAPH.RESULT") != std::string::npos);

    const std::string popOp = functionBlock(graph, "menuGraphFirePopOp");
    REQUIRE_FALSE(popOp.empty());
    REQUIRE(popOp.find("edge->kind != MENU_GRAPH_DEST_POP_TO_PARENT") != std::string::npos);
    REQUIRE(popOp.find("edge->kind != MENU_GRAPH_DEST_POP_TO_ROOT") != std::string::npos);
    REQUIRE(popOp.find("MENU.GRAPH.RESULT") != std::string::npos);

    const std::string localOp = functionBlock(graph, "menuGraphFireLocalOp");
    REQUIRE_FALSE(localOp.empty());
    REQUIRE(localOp.find("edge->kind != MENU_GRAPH_DEST_LOCAL_OP") != std::string::npos);
    REQUIRE(localOp.find("edge->payload.local_op") != std::string::npos);
    REQUIRE(localOp.find("MENU.GRAPH.RESULT") != std::string::npos);

    REQUIRE(mainmenu.find("menuGraphFirePushDialog(MENU_TYPE_MAIN_MENU, \"change_agent\"") != std::string::npos);
    REQUIRE(mainmenu.find("menuGraphFirePushDialog(MENU_TYPE_MAIN_MENU, \"cheats\"") != std::string::npos);
    REQUIRE(mainmenu.find("menuPushDialog(&g_ChangeAgentMenuDialog)") == std::string::npos);
    REQUIRE(mainmenu.find("menuPushDialog(&g_CheatsMenuDialog)") == std::string::npos);
}

TEST_CASE("menu graph: Network menu uses graph helpers for network transitions", "[input][menu_graph][network][static]")
{
    const std::string network = readTextFile("port/fast3d/pdgui_menu_network.cpp");

    REQUIRE_FALSE(network.empty());
    REQUIRE(network.find("#include \"menugraph.h\"") != std::string::npos);

    const std::string render = functionBlock(network, "renderMultiplayerMenu");
    REQUIRE_FALSE(render.empty());

    REQUIRE(render.find("menuGraphFireNetworkOp(MENU_TYPE_NETWORK, \"disconnect\"") != std::string::npos);
    REQUIRE(render.find("menuGraphFireNetworkOp(MENU_TYPE_NETWORK, \"host\"") != std::string::npos);
    REQUIRE(render.find("menuGraphFireNetworkOp(MENU_TYPE_NETWORK, \"join\"") != std::string::npos);
    REQUIRE(render.find("menuGraphFirePushDialog(MENU_TYPE_NETWORK, \"joining\"") != std::string::npos);
    REQUIRE(render.find("menuGraphFirePop(MENU_TYPE_NETWORK, \"host_started\")") != std::string::npos);
    REQUIRE(render.find("menuGraphFirePop(MENU_TYPE_NETWORK, \"back\")") != std::string::npos);

    REQUIRE(render.find("netDisconnect()") == std::string::npos);
    REQUIRE(render.find("netStartServer(") == std::string::npos);
    REQUIRE(render.find("netStartClientWithHolePunch(") == std::string::npos);
    REQUIRE(render.find("menuPushDialog(&g_NetJoiningDialog)") == std::string::npos);
    REQUIRE(render.find("menuPopDialog()") == std::string::npos);
}

TEST_CASE("menu graph: Main Menu no longer exposes legacy Online Play", "[input][menu_graph][network][static][c3828]")
{
    const std::string mainmenu = readTextFile("port/fast3d/pdgui_menu_mainmenu.cpp");
    const std::string graph = readTextFile("port/src/menugraph.c");

    REQUIRE_FALSE(mainmenu.empty());
    REQUIRE_FALSE(graph.empty());

    const std::string render = functionBlock(mainmenu, "renderMainMenu");
    REQUIRE_FALSE(render.empty());

    REQUIRE(render.find("PdButton(\"Play\"") != std::string::npos);
    REQUIRE(render.find("PdButton(\"Solo Play\"") == std::string::npos);
    REQUIRE(render.find("PdButton(\"Online Play\"") == std::string::npos);
    REQUIRE(render.find("menuGraphFireNetworkOp(MENU_TYPE_MAIN_ONLINE_VIEW") == std::string::npos);
    REQUIRE(mainmenu.find("pdguiMainMenuGraphStartClient") == std::string::npos);
    REQUIRE(graph.find("EDGE_PUSH(\"solo_play\", ACTION_MENU_ACCEPT, \"Play\"") != std::string::npos);
    REQUIRE(graph.find("online_play") == std::string::npos);
    REQUIRE(graph.find("NODE(MENU_TYPE_MAIN_ONLINE_VIEW") == std::string::npos);
    REQUIRE(render.find("netStartClientWithHolePunch(") == std::string::npos);
}

TEST_CASE("menu graph: Social Lobby create and disconnect use graph network edges", "[input][menu_graph][network][static]")
{
    const std::string lobby = readTextFile("port/fast3d/pdgui_menu_lobby.cpp");

    REQUIRE_FALSE(lobby.empty());
    REQUIRE(lobby.find("#include \"menugraph.h\"") != std::string::npos);
    REQUIRE(lobby.find("lobbyGraphCreateRoom") != std::string::npos);
    REQUIRE(lobby.find("lobbyGraphDisconnect") != std::string::npos);

    const std::string render = functionBlock(lobby, "pdguiLobbyScreenRender");
    REQUIRE_FALSE(render.empty());
    REQUIRE(render.find("menuGraphFireNetworkOp(MENU_TYPE_SOCIAL_LOBBY, \"create_room\"") != std::string::npos);
    REQUIRE(render.find("menuGraphFireNetworkOp(MENU_TYPE_SOCIAL_LOBBY, \"disconnect\"") != std::string::npos);
    REQUIRE(render.find("netmsgClcRoomCreateWrite(") == std::string::npos);
    REQUIRE(render.find("netDisconnect()") == std::string::npos);
}

TEST_CASE("menu graph: Room setup subdialogs use graph push edges", "[input][menu_graph][room][static]")
{
    const std::string room = readTextFile("port/fast3d/pdgui_menu_room.cpp");

    REQUIRE_FALSE(room.empty());
    REQUIRE(room.find("#include \"menugraph.h\"") != std::string::npos);

    const std::string combat = functionBlock(room, "renderCombatSimTab");
    REQUIRE_FALSE(combat.empty());

    REQUIRE(combat.find("menuGraphFirePushDialog(MENU_TYPE_ROOM, \"team_setup\"") != std::string::npos);
    REQUIRE(combat.find("menuGraphFirePushDialog(MENU_TYPE_ROOM, \"select_music\"") != std::string::npos);
    REQUIRE(combat.find("menuPushDialog(&g_MpTeamsMenuDialog)") == std::string::npos);
    REQUIRE(combat.find("menuPushDialog(&g_MpSelectTunesMenuDialog)") == std::string::npos);
}

TEST_CASE("menu graph: Room Start Match uses scene graph edge", "[input][menu_graph][room][static]")
{
    const std::string room = readTextFile("port/fast3d/pdgui_menu_room.cpp");

    REQUIRE_FALSE(room.empty());
    REQUIRE(room.find("#include \"pdgui_nav.h\"") != std::string::npos);

    size_t renderStart = room.find("extern \"C\" void pdguiRoomScreenRender");
    REQUIRE(renderStart != std::string::npos);

    const std::string op = functionBlock(room, "roomGraphStartMatch");
    const std::string render = functionBlock(room.substr(renderStart), "pdguiRoomScreenRender");
    REQUIRE_FALSE(op.empty());
    REQUIRE_FALSE(render.empty());

    REQUIRE(op.find("matchStart()") != std::string::npos);
    REQUIRE(op.find("netLobbyRequestStartWithSims(") != std::string::npos);
    REQUIRE(op.find("netLobbyRequestStart(") != std::string::npos);
    REQUIRE(render.find("menuGraphFireSceneOp(MENU_TYPE_ROOM, \"start_match\"") != std::string::npos);
    REQUIRE(render.find("matchStart()") == std::string::npos);
    REQUIRE(render.find("netLobbyRequestStartWithSims(") == std::string::npos);
    REQUIRE(render.find("netLobbyRequestStart(") == std::string::npos);
}

TEST_CASE("menu input: Combat Sim room controller parity is wired", "[input][menu_graph][room][static][c086]")
{
    const std::string room = readTextFile("port/fast3d/pdgui_menu_room.cpp");
    const std::string theme = readTextFile("port/fast3d/pdgui_menu_theme_editor.cpp");

    REQUIRE_FALSE(room.empty());
    REQUIRE_FALSE(theme.empty());

    REQUIRE(room.find("ROOM_CS_ARENA") != std::string::npos);
    REQUIRE(room.find("ROOM_CS_SCENARIO") != std::string::npos);
    REQUIRE(room.find("ROOM_CS_LIMITS") != std::string::npos);
    REQUIRE(room.find("ROOM_CS_WEAPONS") != std::string::npos);
    REQUIRE(room.find("ROOM_CS_OPTIONS") != std::string::npos);
    REQUIRE(room.find("roomCsHandlePendingSectionJump()") != std::string::npos);
    REQUIRE(room.find("s_RoomCsSectionJumpPending = skipDir") != std::string::npos);
    REQUIRE(room.find("s_RoomPlayerSectionJumpPending = skipDir") != std::string::npos);
    REQUIRE(room.find("s_StartMatchFocusPending = true") != std::string::npos);
    REQUIRE(room.find("pdguiMenuStartPressed()") != std::string::npos);
    REQUIRE(room.find("roomContextPopupRequestedForLastItem()") != std::string::npos);
    REQUIRE(room.find("ImGui::OpenPopup(\"##local_ctx\")") != std::string::npos);
    REQUIRE(room.find("ImGui::OpenPopup(\"##add_bot_ctx\")") != std::string::npos);
    REQUIRE(room.find("Fill Bot Slots") != std::string::npos);
    REQUIRE(room.find("Random name") != std::string::npos);
    REQUIRE(room.find("Re-Roll Name") == std::string::npos);
    REQUIRE(room.find("TreeNodeEx(") == std::string::npos);
    REQUIRE(room.find("CollapsingHeader(") == std::string::npos);
    REQUIRE(theme.find("ImGui::BeginChild(\"PaletteScroll\"") != std::string::npos);
    REQUIRE(theme.find("ImGuiChildFlags_Border | ImGuiChildFlags_NavFlattened") != std::string::npos);
}

TEST_CASE("menu input: Y-Social is first-class only on main and pause menus", "[input][menu_graph][social][static][c087][c088]")
{
    const std::string mainmenu = readTextFile("port/fast3d/pdgui_menu_mainmenu.cpp");
    const std::string pause = readTextFile("port/fast3d/pdgui_menu_pausemenu.cpp");
    const std::string room = readTextFile("port/fast3d/pdgui_menu_room.cpp");

    REQUIRE_FALSE(mainmenu.empty());
    REQUIRE_FALSE(pause.empty());
    REQUIRE_FALSE(room.empty());

    const std::string mainRender = functionBlock(mainmenu, "renderMainMenu");
    const std::string pauseRender = functionBlock(pause, "pdguiPauseMenuRender");
    REQUIRE_FALSE(mainRender.empty());
    REQUIRE_FALSE(pauseRender.empty());

    REQUIRE(mainRender.find("pdguiMenuTertiaryPressed()") != std::string::npos);
    REQUIRE(mainRender.find("!pdguiFriendsSocialIsOpen()") != std::string::npos);
    REQUIRE(mainRender.find("menuGraphFirePushOp(MENU_TYPE_MAIN_MENU, \"social\"") != std::string::npos);
    REQUIRE(mainRender.find("pdguiDrawActionPromptCentered(ACTION_MENU_SOCIAL") != std::string::npos);
    REQUIRE(mainRender.find("!socialSurfaceOpen") != std::string::npos);

    REQUIRE(pause.find("#include \"pdgui_friends.h\"") != std::string::npos);
    REQUIRE(pause.find("#include \"pdgui_glyphs.h\"") != std::string::npos);
    REQUIRE(pauseRender.find("pdguiMenuTertiaryPressed()") != std::string::npos);
    REQUIRE(pauseRender.find("pdguiFriendsSocialOpen()") != std::string::npos);
    REQUIRE(pauseRender.find("pdguiDrawActionPromptCentered(ACTION_MENU_SOCIAL") != std::string::npos);
    REQUIRE(pauseRender.find("!socialSurfaceOpen && !endgamePopupWasOpen && !shortcutsPopupWasOpen") != std::string::npos);

    REQUIRE(room.find("do NOT poll pdguiMenuTertiaryPressed") != std::string::npos);
    REQUIRE(room.find("pdguiMenuTertiaryPressed()") == std::string::npos);
}

TEST_CASE("menu graph: Room Leave uses graph network edge", "[input][menu_graph][room][static]")
{
    const std::string room = readTextFile("port/fast3d/pdgui_menu_room.cpp");

    REQUIRE_FALSE(room.empty());

    size_t renderStart = room.find("extern \"C\" void pdguiRoomScreenRender");
    REQUIRE(renderStart != std::string::npos);

    const std::string op = functionBlock(room, "roomGraphLeaveRoom");
    const std::string render = functionBlock(room.substr(renderStart), "pdguiRoomScreenRender");
    REQUIRE_FALSE(op.empty());
    REQUIRE_FALSE(render.empty());

    REQUIRE(op.find("menupoolRelease(MENU_TYPE_ROOM)") != std::string::npos);
    REQUIRE(op.find("pdguiSoloRoomClose()") != std::string::npos);
    REQUIRE(op.find("netmsgClcRoomLeaveWrite(&g_NetMsgRel)") != std::string::npos);
    REQUIRE(op.find("netListenHostRoomLeave()") != std::string::npos);
    REQUIRE(op.find("pdguiSetInRoom(0)") != std::string::npos);
    REQUIRE(render.find("menuGraphFireNetworkOp(MENU_TYPE_ROOM, \"leave_room\"") != std::string::npos);
    REQUIRE(render.find("netmsgClcRoomLeaveWrite(&g_NetMsgRel)") == std::string::npos);
    REQUIRE(render.find("netListenHostRoomLeave()") == std::string::npos);
    REQUIRE(render.find("pdguiSetInRoom(0)") == std::string::npos);
    REQUIRE(render.find("pdguiMenuAcceptPressed()") != std::string::npos);
    REQUIRE(render.find("pdguiMenuCancelPressed()") != std::string::npos);
    requireNoRawMenuShortcutPolling(render);
}

TEST_CASE("menu graph: warning modal close paths use graph pop edges", "[input][menu_graph][warning][static]")
{
    const std::string warning = readTextFile("port/fast3d/pdgui_menu_warning.cpp");

    REQUIRE_FALSE(warning.empty());
    REQUIRE(warning.find("#include \"menugraph.h\"") != std::string::npos);
    REQUIRE(warning.find("#include \"pdgui_nav.h\"") != std::string::npos);

    const std::string typed = functionBlock(warning, "renderTypedDialog");
    const std::string endgame = functionBlock(warning, "renderMpEndGameDialog");
    const std::string filemgr = functionBlock(warning, "renderFilemgrPcPlaceholder");
    REQUIRE_FALSE(typed.empty());
    REQUIRE_FALSE(endgame.empty());
    REQUIRE_FALSE(filemgr.empty());

    REQUIRE(typed.find("menuGraphFirePop(MENU_TYPE_WARNING_MODAL, \"confirm\")") != std::string::npos);
    REQUIRE(typed.find("menuGraphFirePop(MENU_TYPE_WARNING_MODAL, \"cancel\")") != std::string::npos);
    REQUIRE(endgame.find("menuGraphFirePop(MENU_TYPE_WARNING_MODAL, \"confirm\")") != std::string::npos);
    REQUIRE(endgame.find("menuGraphFirePop(MENU_TYPE_WARNING_MODAL, \"cancel\")") != std::string::npos);
    REQUIRE(filemgr.find("menuGraphFirePop(MENU_TYPE_WARNING_MODAL, \"confirm\")") != std::string::npos);
    REQUIRE(filemgr.find("menuGraphFirePop(MENU_TYPE_WARNING_MODAL, \"cancel\")") != std::string::npos);
    REQUIRE(typed.find("pdguiMenuCancelPressed()") != std::string::npos);
    REQUIRE(endgame.find("pdguiMenuAcceptPressed()") != std::string::npos);
    REQUIRE(endgame.find("pdguiMenuCancelPressed()") != std::string::npos);
    REQUIRE(filemgr.find("pdguiMenuCancelPressed()") != std::string::npos);
    requireNoRawMenuShortcutPolling(typed);
    requireNoRawMenuShortcutPolling(endgame);
    requireNoRawMenuShortcutPolling(filemgr);

    REQUIRE(warning.find("menuPopDialog()") == std::string::npos);
    REQUIRE(warning.find("void menuPopDialog") == std::string::npos);
}

TEST_CASE("menu graph: solo endscreen scene transitions use graph edges", "[input][menu_graph][endscreen][static]")
{
    const std::string endscreen = readTextFile("port/fast3d/pdgui_menu_endscreen.cpp");
    const std::string graph = readTextFile("port/src/menugraph.c");

    REQUIRE_FALSE(endscreen.empty());
    REQUIRE_FALSE(graph.empty());
    REQUIRE(endscreen.find("#include \"menugraph.h\"") != std::string::npos);
    REQUIRE(graph.find("EDGE_SCENE(\"continue\", ACTION_MENU_ACCEPT, \"Continue\", SCENE_EVENT_GAMEPLAY_START)") != std::string::npos);
    REQUIRE(graph.find("EDGE_SCENE(\"retry\", ACTION_MENU_ACCEPT, \"Retry\", SCENE_EVENT_GAMEPLAY_START)") != std::string::npos);
    REQUIRE(graph.find("EDGE_SCENE(\"main_menu\", ACTION_MENU_CANCEL, \"Main Menu\", SCENE_EVENT_STAGE_TEARDOWN)") != std::string::npos);

    const std::string next = functionBlock(endscreen, "endscreenGraphNextMission");
    const std::string retry = functionBlock(endscreen, "endscreenGraphRetryMission");
    const std::string exit = functionBlock(endscreen, "endscreenGraphExitToMainMenu");
    const std::string render = functionBlock(endscreen, "renderSoloEndscreen");
    REQUIRE_FALSE(next.empty());
    REQUIRE_FALSE(retry.empty());
    REQUIRE_FALSE(exit.empty());
    REQUIRE_FALSE(render.empty());

    REQUIRE(next.find("pdguiEndscreenNextMission()") != std::string::npos);
    REQUIRE(retry.find("pdguiEndscreenStartMission()") != std::string::npos);
    REQUIRE(exit.find("pdguiEndscreenExitToMainMenu()") != std::string::npos);
    REQUIRE(render.find("menuGraphFireSceneOp(MENU_TYPE_ENDSCREEN_SOLO, \"continue\"") != std::string::npos);
    REQUIRE(render.find("menuGraphFireSceneOp(MENU_TYPE_ENDSCREEN_SOLO, \"retry\"") != std::string::npos);
    REQUIRE(render.find("menuGraphFireSceneOp(MENU_TYPE_ENDSCREEN_SOLO, \"main_menu\"") != std::string::npos);
    REQUIRE(render.find("pdguiEndscreenNextMission()") == std::string::npos);
    REQUIRE(render.find("pdguiEndscreenStartMission()") == std::string::npos);
    REQUIRE(render.find("pdguiEndscreenExitToMainMenu()") == std::string::npos);
    REQUIRE(render.find("bool wantSoloMainMenuConfirm") != std::string::npos);
    REQUIRE(findAll(render, "ImGui::OpenPopup(sfmmPopupId)").size() == 1);
}

TEST_CASE("menu graph: MP endscreen exits use graph edges", "[input][menu_graph][endscreen][static]")
{
    const std::string endscreen = readTextFile("port/fast3d/pdgui_menu_endscreen.cpp");
    const std::string graph = readTextFile("port/src/menugraph.c");

    REQUIRE_FALSE(endscreen.empty());
    REQUIRE_FALSE(graph.empty());
    REQUIRE(endscreen.find("#include \"menugraph.h\"") != std::string::npos);
    REQUIRE(graph.find("EDGE_SCENE(\"continue\", ACTION_MENU_ACCEPT, \"Continue\", SCENE_EVENT_STAGE_TEARDOWN)") != std::string::npos);
    REQUIRE(graph.find("EDGE_NETWORK(\"disconnect\", ACTION_MENU_CANCEL, \"Disconnect\", \"disconnect\")") != std::string::npos);
    REQUIRE(graph.find("EDGE_SCENE(\"quit\", ACTION_MENU_CANCEL, \"Quit\", SCENE_EVENT_STAGE_TEARDOWN)") != std::string::npos);

    const std::string cont = functionBlock(endscreen, "endscreenGraphMpContinue");
    const std::string disconnect = functionBlock(endscreen, "endscreenGraphDisconnect");
    REQUIRE(endscreen.find("endscreenGraphDisconnect") != std::string::npos);
    REQUIRE_FALSE(cont.empty());
    REQUIRE_FALSE(disconnect.empty());
    REQUIRE(cont.find("pdguiEndscreenExitToMainMenu()") != std::string::npos);
    REQUIRE(cont.find("pdguiSetInRoom(1)") != std::string::npos);
    REQUIRE(cont.find("pdguiSoloRoomReturn()") != std::string::npos);
    REQUIRE(disconnect.find("netDisconnect()") != std::string::npos);
    REQUIRE(disconnect.find("pdguiEndscreenExitToMainMenu()") != std::string::npos);

    const std::string render = functionBlock(endscreen, "renderMpEndscreen");
    REQUIRE_FALSE(render.empty());
    REQUIRE(render.find("menuGraphFireSceneOp(MENU_TYPE_ENDSCREEN_MP, \"continue\"") != std::string::npos);
    REQUIRE(render.find("menuGraphFireNetworkOp(MENU_TYPE_ENDSCREEN_MP, \"disconnect\"") != std::string::npos);
    REQUIRE(render.find("menuGraphFireSceneOp(MENU_TYPE_ENDSCREEN_MP, \"quit\"") != std::string::npos);
    REQUIRE(render.find("netDisconnect()") == std::string::npos);
    REQUIRE(render.find("pdguiEndscreenExitToMainMenu()") == std::string::npos);
    REQUIRE(render.find("pdguiSetInRoom(1)") == std::string::npos);
    REQUIRE(render.find("pdguiSoloRoomReturn()") == std::string::npos);
    REQUIRE(render.find("bool wantDisconnectConfirm") != std::string::npos);
    REQUIRE(render.find("bool wantQuitConfirm") != std::string::npos);
    REQUIRE(findAll(render, "ImGui::OpenPopup(mpDisconnectPopupId)").size() == 1);
    REQUIRE(findAll(render, "ImGui::OpenPopup(mpQuitPopupId)").size() == 1);
}

TEST_CASE("menu graph: Agent Select load create and back use graph helpers", "[input][menu_graph][agent][static]")
{
    const std::string agent = readTextFile("port/fast3d/pdgui_menu_agentselect.cpp");
    const std::string graph = readTextFile("port/src/menugraph.c");

    REQUIRE_FALSE(agent.empty());
    REQUIRE_FALSE(graph.empty());
    REQUIRE(agent.find("#include \"menugraph.h\"") != std::string::npos);
    REQUIRE(graph.find("EDGE_LOCAL(\"load\", ACTION_MENU_ACCEPT, \"Load Agent\", \"load_agent\")") != std::string::npos);

    const std::string render = functionBlock(agent, "renderAgentSelect");
    const std::string load = functionBlock(agent, "agentSelectGraphLoad");
    REQUIRE_FALSE(load.empty());
    REQUIRE_FALSE(render.empty());

    REQUIRE(load.find("menupoolReleaseDialog(payload->release_def)") != std::string::npos);
    REQUIRE(load.find("filemgrSaveOrLoad(&g_GameFileGuid, FILEOP_LOAD_GAME, 0)") != std::string::npos);
    REQUIRE(load.find("prefsLoadForFile(payload->file)") != std::string::npos);
    REQUIRE(render.find("menuGraphFireLocalOp(MENU_TYPE_AGENT_SELECT, \"load\"") != std::string::npos);
    REQUIRE(findAll(render, "menuGraphFireLocalOp(MENU_TYPE_AGENT_SELECT, \"load\"").size() >= 2);
    REQUIRE(render.find("menuGraphFirePushDialog(MENU_TYPE_AGENT_SELECT, \"create\"") != std::string::npos);
    REQUIRE(render.find("menuGraphFirePop(MENU_TYPE_AGENT_SELECT, \"back\")") != std::string::npos);
    REQUIRE(render.find("menuPushDialog(&g_FilemgrEnterNameMenuDialog)") == std::string::npos);
    REQUIRE(render.find("menuPopDialog()") == std::string::npos);
}

TEST_CASE("menu graph: Firing Range difficulty start and cancel use graph edges", "[input][menu_graph][training][static]")
{
    const std::string training = readTextFile("port/fast3d/pdgui_menu_training.cpp");
    const std::string graph = readTextFile("port/src/menugraph.c");

    REQUIRE_FALSE(training.empty());
    REQUIRE_FALSE(graph.empty());
    REQUIRE(training.find("#include \"menugraph.h\"") != std::string::npos);
    REQUIRE(graph.find("EDGE_PUSH(\"start\", ACTION_MENU_ACCEPT, \"Start Firing Range\", MENU_TYPE_FR_INFO)") != std::string::npos);
    REQUIRE(graph.find("EDGE_POP(\"cancel\", ACTION_MENU_CANCEL, \"Cancel\")") != std::string::npos);
    REQUIRE(graph.find("NODE(MENU_TYPE_FR_DIFFICULTY, \"fr_difficulty\", s_FrDifficultyEdges)") != std::string::npos);

    const std::string open = functionBlock(training, "frDifficultyOpenPreGame");
    const std::string render = functionBlock(training, "renderFrDifficulty");
    REQUIRE_FALSE(open.empty());
    REQUIRE_FALSE(render.empty());

    REQUIRE(open.find("frSetDifficulty(difficulty)") != std::string::npos);
    REQUIRE(open.find("menuGraphFirePushDialog(MENU_TYPE_FR_DIFFICULTY, \"start\"") != std::string::npos);
    REQUIRE(render.find("frDifficultyOpenPreGame(FRDIFFICULTY_BRONZE)") != std::string::npos);
    REQUIRE(render.find("frDifficultyOpenPreGame(FRDIFFICULTY_SILVER)") != std::string::npos);
    REQUIRE(render.find("frDifficultyOpenPreGame(FRDIFFICULTY_GOLD)") != std::string::npos);
    REQUIRE(render.find("menuGraphFirePop(MENU_TYPE_FR_DIFFICULTY, \"cancel\")") != std::string::npos);
    REQUIRE(render.find("menuPushDialog(&g_FrTrainingInfoPreGameMenuDialog)") == std::string::npos);
    REQUIRE(render.find("menuPopDialog()") == std::string::npos);
}

TEST_CASE("menu graph: MP pause resume and End Game use graph helpers", "[input][menu_graph][pause][static]")
{
    const std::string pause = readTextFile("port/fast3d/pdgui_menu_mppause.cpp");

    REQUIRE_FALSE(pause.empty());
    REQUIRE(pause.find("#include \"menugraph.h\"") != std::string::npos);
    REQUIRE(pause.find("#include \"pdgui_nav.h\"") != std::string::npos);

    const std::string close = functionBlock(pause, "mpp_CloseCurrentDialog");
    const std::string push = functionBlock(pause, "hubPushRow");
    REQUIRE_FALSE(close.empty());
    REQUIRE_FALSE(push.empty());

    REQUIRE(close.find("menuGraphFirePop(MENU_TYPE_MP_PAUSE, \"resume\")") != std::string::npos);
    REQUIRE(close.find("menuPopDialog()") == std::string::npos);
    REQUIRE(push.find("menuGraphFirePushDialog(MENU_TYPE_MP_PAUSE, \"end_game\"") != std::string::npos);
    REQUIRE(push.find("menuPushDialog(target)") == std::string::npos);
}

TEST_CASE("menu graph: combat-sim pause End Match uses scene graph edge", "[input][menu_graph][pause][static]")
{
    const std::string pause = readTextFile("port/fast3d/pdgui_menu_pausemenu.cpp");

    REQUIRE_FALSE(pause.empty());
    REQUIRE(pause.find("#include \"menugraph.h\"") != std::string::npos);

    const std::string op = functionBlock(pause, "pauseGraphEndMission");
    const std::string render = functionBlock(pause, "pdguiPauseMenuRender");
    REQUIRE_FALSE(op.empty());
    REQUIRE_FALSE(render.empty());

    REQUIRE(op.find("pdguiPauseSetPlayerAborted()") != std::string::npos);
    REQUIRE(op.find("mainEndStage()") != std::string::npos);
    REQUIRE(render.find("menuGraphFireSceneOp(MENU_TYPE_PAUSE_MENU, \"end_mission\"") != std::string::npos);
    REQUIRE(render.find("pdguiPauseSetPlayerAborted()") == std::string::npos);
    REQUIRE(render.find("mainEndStage()") == std::string::npos);
    REQUIRE(render.find("pdguiMenuAcceptPressed()") != std::string::npos);
    REQUIRE(render.find("pdguiMenuCancelPressed()") != std::string::npos);
    requireNoRawMenuShortcutPolling(render);
}

TEST_CASE("menu graph: solo mission start back and restart use graph edges", "[input][menu_graph][solo][static]")
{
    const std::string solo = readTextFile("port/fast3d/pdgui_menu_solomission.cpp");
    const std::string graph = readTextFile("port/src/menugraph.c");

    REQUIRE_FALSE(solo.empty());
    REQUIRE_FALSE(graph.empty());
    REQUIRE(solo.find("#include \"menugraph.h\"") != std::string::npos);
    REQUIRE(graph.find("EDGE_SCENE(\"start\", ACTION_MENU_ACCEPT, \"Start Mission\", SCENE_EVENT_GAMEPLAY_START)") != std::string::npos);
    REQUIRE(graph.find("EDGE_SCENE(\"restart\", ACTION_MENU_ACCEPT, \"Restart Mission\", SCENE_EVENT_GAMEPLAY_START)") != std::string::npos);
    REQUIRE(graph.find("EDGE_POP(\"back\", ACTION_MENU_CANCEL, \"Back\")") != std::string::npos);

    const std::string start = functionBlock(solo, "soloMissionGraphStart");
    const std::string restart = functionBlock(solo, "soloMissionGraphRestart");
    const std::string mission = functionBlock(solo, "renderMissionSelect");
    const std::string accept = functionBlock(solo, "renderAcceptMission");
    const std::string pause = functionBlock(solo, "renderPauseMenu");
    REQUIRE_FALSE(start.empty());
    REQUIRE_FALSE(restart.empty());
    REQUIRE_FALSE(mission.empty());
    REQUIRE_FALSE(accept.empty());
    REQUIRE_FALSE(pause.empty());

    REQUIRE(start.find("menuhandlerAcceptMission(MENUOP_SET") != std::string::npos);
    REQUIRE(start.find("inputCtxPopDeferred(&g_CtxImGuiMenu)") != std::string::npos);
    REQUIRE(restart.find("catalogResolveStage(g_MissionConfig.stage_id, &sr)") != std::string::npos);
    REQUIRE(restart.find("mainChangeToStage(rsn)") != std::string::npos);

    REQUIRE(mission.find("menuGraphFireSceneOp(MENU_TYPE_SOLO_MISSION, \"start\"") != std::string::npos);
    REQUIRE(mission.find("menuGraphFirePop(MENU_TYPE_SOLO_MISSION, \"back\")") != std::string::npos);
    REQUIRE(mission.find("menuhandlerAcceptMission(MENUOP_SET") == std::string::npos);
    REQUIRE(mission.find("menuPopDialog()") == std::string::npos);

    REQUIRE(accept.find("menuGraphFireSceneOp(MENU_TYPE_SOLO_MISSION, \"start\"") != std::string::npos);
    REQUIRE(accept.find("menuGraphFirePop(MENU_TYPE_SOLO_MISSION, \"back\")") != std::string::npos);
    REQUIRE(accept.find("menuhandlerAcceptMission(MENUOP_SET") == std::string::npos);
    REQUIRE(accept.find("menuPopDialog()") == std::string::npos);

    REQUIRE(pause.find("menuGraphFireSceneOp(MENU_TYPE_SOLO_MISSION_PAUSE, \"restart\"") != std::string::npos);
    REQUIRE(pause.find("mainChangeToStage(") == std::string::npos);
}

TEST_CASE("menu graph: solo mission pause uses graph helpers for pause subdialogs", "[input][menu_graph][pause][static]")
{
    const std::string solo = readTextFile("port/fast3d/pdgui_menu_solomission.cpp");
    const std::string pool_h = readTextFile("port/include/menupool.h");
    const std::string pool_c = readTextFile("port/src/menupool.c");
    const std::string menu_h = readTextFile("src/include/game/menu.h");
    const std::string menu_c = readTextFile("src/game/menu.c");

    REQUIRE_FALSE(solo.empty());
    REQUIRE_FALSE(pool_h.empty());
    REQUIRE_FALSE(pool_c.empty());
    REQUIRE_FALSE(menu_h.empty());
    REQUIRE_FALSE(menu_c.empty());

    REQUIRE(solo.find("#include \"menugraph.h\"") != std::string::npos);
    REQUIRE(pool_h.find("MENU_TYPE_SOLO_INVENTORY") != std::string::npos);
    REQUIRE(pool_c.find("REG(&g_SoloMissionInventoryMenuDialog, MENU_TYPE_SOLO_INVENTORY)") != std::string::npos);
    REQUIRE(pool_c.find("REG(&g_SoloMissionOptionsMenuDialog, MENU_TYPE_SOLO_OPTIONS)") != std::string::npos);
    REQUIRE(menu_h.find("menuSwitchToDialog") != std::string::npos);
    REQUIRE(menu_c.find("s32 menuSwitchToDialog(struct menudialogdef *dialogdef)") != std::string::npos);

    const std::string pause = functionBlock(solo, "renderPauseMenu");
    const std::string inventory = functionBlock(solo, "renderInventory");
    const std::string options = functionBlock(solo, "renderOptions");
    REQUIRE_FALSE(pause.empty());
    REQUIRE_FALSE(inventory.empty());
    REQUIRE_FALSE(options.empty());

    REQUIRE(pause.find("menuGraphFirePop(MENU_TYPE_SOLO_MISSION_PAUSE, \"resume\")") != std::string::npos);
    REQUIRE(pause.find("menuGraphFireSwitchSibling(MENU_TYPE_SOLO_MISSION_PAUSE, \"inventory\"") != std::string::npos);
    REQUIRE(pause.find("menuGraphFireSwitchSibling(MENU_TYPE_SOLO_MISSION_PAUSE, \"settings\"") != std::string::npos);
    REQUIRE(pause.find("menuGraphFirePushDialog(MENU_TYPE_SOLO_MISSION_PAUSE, \"abort\"") != std::string::npos);
    REQUIRE(inventory.find("menuGraphFireSwitchSibling(MENU_TYPE_SOLO_INVENTORY, \"back\"") != std::string::npos);
    REQUIRE(options.find("menuGraphFireSwitchSibling(MENU_TYPE_SOLO_OPTIONS, \"back\"") != std::string::npos);
    REQUIRE(pause.find("menuPushDialog(&g_SoloMissionInventoryMenuDialog)") == std::string::npos);
    REQUIRE(pause.find("menuPushDialog(&g_SoloMissionOptionsMenuDialog)") == std::string::npos);
    REQUIRE(pause.find("menuPushDialog(&g_MissionAbortMenuDialog)") == std::string::npos);
    REQUIRE(inventory.find("menuPopDialog()") == std::string::npos);
    REQUIRE(options.find("menuPopDialog()") == std::string::npos);
}

/* -------------------------------------------------------------------------
 * c036 / s036-08 menu-graph completion slice -- four small priority nodes.
 *
 * Adds graph nodes + edges + call-site migrations for four screens whose
 * only remaining raw menuPopDialog() shortcut had not yet been migrated:
 *   - MENU_TYPE_AGENT_CREATE       (save / cancel pops)
 *   - MENU_TYPE_CHALLENGES         (back pop)
 *   - MENU_TYPE_MP_TEAM_SETUP      (done pop)
 *   - MENU_TYPE_MP_PLAYER_CONFIG   (close pop)
 *
 * Mirrors the L.16-L.55 slice pattern: declare the graph edges, route the
 * legacy pop through menuGraphFirePop, and pin the renderer against
 * re-introducing raw menuPopDialog() in the migrated function blocks.
 * ------------------------------------------------------------------------- */

TEST_CASE("menu graph: Agent Create save and cancel use graph pop edges", "[input][menu_graph][agent][static]")
{
    const std::string create = readTextFile("port/fast3d/pdgui_menu_agentcreate.cpp");
    const std::string graph = readTextFile("port/src/menugraph.c");

    REQUIRE_FALSE(create.empty());
    REQUIRE_FALSE(graph.empty());
    REQUIRE(create.find("#include \"menugraph.h\"") != std::string::npos);
    REQUIRE(graph.find("EDGE_POP(\"save\", ACTION_MENU_ACCEPT, \"Save Agent\")") != std::string::npos);
    REQUIRE(graph.find("EDGE_POP(\"cancel\", ACTION_MENU_CANCEL, \"Cancel\")") != std::string::npos);
    REQUIRE(graph.find("NODE(MENU_TYPE_AGENT_CREATE, \"agent_create\", s_AgentCreateEdges)") != std::string::npos);

    REQUIRE(create.find("menuGraphFirePop(MENU_TYPE_AGENT_CREATE, \"save\")") != std::string::npos);
    REQUIRE(create.find("menuGraphFirePop(MENU_TYPE_AGENT_CREATE, \"cancel\")") != std::string::npos);
    REQUIRE(create.find("menuPopDialog();") == std::string::npos);
}

TEST_CASE("menu graph: Challenges back uses graph pop edge", "[input][menu_graph][challenges][static]")
{
    const std::string challenges = readTextFile("port/fast3d/pdgui_menu_challenges.cpp");
    const std::string graph = readTextFile("port/src/menugraph.c");

    REQUIRE_FALSE(challenges.empty());
    REQUIRE_FALSE(graph.empty());
    REQUIRE(challenges.find("#include \"menugraph.h\"") != std::string::npos);
    REQUIRE(graph.find("NODE(MENU_TYPE_CHALLENGES, \"challenges\", s_ChallengesEdges)") != std::string::npos);

    const std::string render = functionBlock(challenges, "renderChallenges");
    REQUIRE_FALSE(render.empty());
    REQUIRE(render.find("menuGraphFirePop(MENU_TYPE_CHALLENGES, \"back\")") != std::string::npos);
    REQUIRE(render.find("menuPopDialog();") == std::string::npos);
}

TEST_CASE("menu graph: MP Team Setup done uses graph pop edge", "[input][menu_graph][teamsetup][static]")
{
    const std::string team = readTextFile("port/fast3d/pdgui_menu_teamsetup.cpp");
    const std::string graph = readTextFile("port/src/menugraph.c");

    REQUIRE_FALSE(team.empty());
    REQUIRE_FALSE(graph.empty());
    REQUIRE(team.find("#include \"menugraph.h\"") != std::string::npos);
    REQUIRE(graph.find("EDGE_POP(\"done\", ACTION_MENU_ACCEPT, \"Done\")") != std::string::npos);
    REQUIRE(graph.find("NODE(MENU_TYPE_MP_TEAM_SETUP, \"mp_team_setup\", s_MpTeamSetupEdges)") != std::string::npos);

    REQUIRE(team.find("menuGraphFirePop(MENU_TYPE_MP_TEAM_SETUP, \"done\")") != std::string::npos);
    REQUIRE(team.find("menuPopDialog();") == std::string::npos);
}

TEST_CASE("menu graph: MP Player Config close uses graph pop edge", "[input][menu_graph][playerconfig][static]")
{
    const std::string config = readTextFile("port/fast3d/pdgui_menu_playerconfig.cpp");
    const std::string graph = readTextFile("port/src/menugraph.c");

    REQUIRE_FALSE(config.empty());
    REQUIRE_FALSE(graph.empty());
    REQUIRE(config.find("#include \"menugraph.h\"") != std::string::npos);
    REQUIRE(graph.find("EDGE_POP(\"close\", ACTION_MENU_CANCEL, \"Close\")") != std::string::npos);
    REQUIRE(graph.find("NODE(MENU_TYPE_MP_PLAYER_CONFIG, \"mp_player_config\", s_MpPlayerConfigEdges)") != std::string::npos);

    const std::string close = functionBlock(config, "pc_CloseCurrentDialog");
    REQUIRE_FALSE(close.empty());
    REQUIRE(close.find("menuGraphFirePop(MENU_TYPE_MP_PLAYER_CONFIG, \"close\")") != std::string::npos);
    REQUIRE(close.find("menuPopDialog()") == std::string::npos);
}

/* -------------------------------------------------------------------------
 * c036 / s036-08 menu-graph completion slice -- helper-funnel pop migrations
 * (2026-05-14).
 *
 * Adds graph nodes + edges + call-site migrations for three MP screens whose
 * back-pop is funneled through a single per-file close-helper:
 *   - MENU_TYPE_MP_SETUP      (pdgui_menu_mpsetup.cpp / mp_CloseCurrentDialog)
 *   - MENU_TYPE_MP_ADVANCED   (pdgui_menu_mpadvanced.cpp / ma_CloseCurrentDialog)
 *   - MENU_TYPE_MP_BOT_SETUP  (pdgui_menu_botsetup.cpp / bs_CloseCurrentDialog)
 *
 * Each helper services many sibling renderers (Arena / Scenario / Weapons /
 * Limits / Ready under MP_SETUP, Advanced Setup / Quick Go / Quick Team under
 * MP_ADVANCED, Simulants roster + Add / Change / Edit / Character under
 * MP_BOT_SETUP). All callers map to the same pool slot, so a single
 * menuGraphFirePop call inside the helper is the correct migration.
 * ------------------------------------------------------------------------- */

TEST_CASE("menu graph: MP Setup back uses graph pop edge", "[input][menu_graph][mpsetup][static]")
{
    const std::string source = readTextFile("port/fast3d/pdgui_menu_mpsetup.cpp");
    const std::string graph = readTextFile("port/src/menugraph.c");

    REQUIRE_FALSE(source.empty());
    REQUIRE_FALSE(graph.empty());
    REQUIRE(source.find("#include \"menugraph.h\"") != std::string::npos);
    REQUIRE(graph.find("NODE(MENU_TYPE_MP_SETUP, \"mp_setup\", s_MpSetupEdges)") != std::string::npos);

    const std::string close = functionBlock(source, "mp_CloseCurrentDialog");
    REQUIRE_FALSE(close.empty());
    REQUIRE(close.find("menuGraphFirePop(MENU_TYPE_MP_SETUP, \"back\")") != std::string::npos);
    REQUIRE(close.find("menuPopDialog()") == std::string::npos);
}

TEST_CASE("menu graph: MP Advanced back uses graph pop edge", "[input][menu_graph][mpadvanced][static]")
{
    const std::string source = readTextFile("port/fast3d/pdgui_menu_mpadvanced.cpp");
    const std::string graph = readTextFile("port/src/menugraph.c");

    REQUIRE_FALSE(source.empty());
    REQUIRE_FALSE(graph.empty());
    REQUIRE(source.find("#include \"menugraph.h\"") != std::string::npos);
    REQUIRE(graph.find("NODE(MENU_TYPE_MP_ADVANCED, \"mp_advanced\", s_MpAdvancedEdges)") != std::string::npos);

    const std::string close = functionBlock(source, "ma_CloseCurrentDialog");
    REQUIRE_FALSE(close.empty());
    REQUIRE(close.find("menuGraphFirePop(MENU_TYPE_MP_ADVANCED, \"back\")") != std::string::npos);
    REQUIRE(close.find("menuPopDialog()") == std::string::npos);
}

TEST_CASE("menu graph: MP Bot Setup back uses graph pop edge", "[input][menu_graph][botsetup][static]")
{
    const std::string source = readTextFile("port/fast3d/pdgui_menu_botsetup.cpp");
    const std::string graph = readTextFile("port/src/menugraph.c");

    REQUIRE_FALSE(source.empty());
    REQUIRE_FALSE(graph.empty());
    REQUIRE(source.find("#include \"menugraph.h\"") != std::string::npos);
    REQUIRE(graph.find("NODE(MENU_TYPE_MP_BOT_SETUP, \"mp_bot_setup\", s_MpBotSetupEdges)") != std::string::npos);

    const std::string close = functionBlock(source, "bs_CloseCurrentDialog");
    REQUIRE_FALSE(close.empty());
    REQUIRE(close.find("menuGraphFirePop(MENU_TYPE_MP_BOT_SETUP, \"back\")") != std::string::npos);
    REQUIRE(close.find("menuPopDialog()") == std::string::npos);
}

TEST_CASE("menu graph: c036 ImGui menu surface has no raw stack calls", "[input][menu_graph][c036][static]")
{
    const char *paths[] = {
        "port/fast3d/pdgui_menu_agentcreate.cpp",
        "port/fast3d/pdgui_menu_agentselect.cpp",
        "port/fast3d/pdgui_menu_audiomod.cpp",
        "port/fast3d/pdgui_menu_botsetup.cpp",
        "port/fast3d/pdgui_menu_challenges.cpp",
        "port/fast3d/pdgui_menu_cheats.cpp",
        "port/fast3d/pdgui_menu_controldiagram.cpp",
        "port/fast3d/pdgui_menu_endscreen.cpp",
        "port/fast3d/pdgui_menu_forge.cpp",
        "port/fast3d/pdgui_menu_lobby.cpp",
        "port/fast3d/pdgui_menu_logviewer.cpp",
        "port/fast3d/pdgui_menu_mainmenu.cpp",
        "port/fast3d/pdgui_menu_moddinghub.cpp",
        "port/fast3d/pdgui_menu_modmgr.cpp",
        "port/fast3d/pdgui_menu_mpadvanced.cpp",
        "port/fast3d/pdgui_menu_mpingame.cpp",
        "port/fast3d/pdgui_menu_mppause.cpp",
        "port/fast3d/pdgui_menu_mpsettings.cpp",
        "port/fast3d/pdgui_menu_mpsetup.cpp",
        "port/fast3d/pdgui_menu_network.cpp",
        "port/fast3d/pdgui_menu_pausemenu.cpp",
        "port/fast3d/pdgui_menu_playerconfig.cpp",
        "port/fast3d/pdgui_menu_room.cpp",
        "port/fast3d/pdgui_menu_solomission.cpp",
        "port/fast3d/pdgui_menu_stack_debug.cpp",
        "port/fast3d/pdgui_menu_stats.cpp",
        "port/fast3d/pdgui_menu_teamsetup.cpp",
        "port/fast3d/pdgui_menu_theme_editor.cpp",
        "port/fast3d/pdgui_menu_training.cpp",
        "port/fast3d/pdgui_menu_update.cpp",
        "port/fast3d/pdgui_menu_warning.cpp",
    };

    for (const char *path : paths) {
        const std::string source = readTextFile(path);
        REQUIRE_FALSE(source.empty());
        REQUIRE(source.find("menuPushDialog(") == std::string::npos);
        REQUIRE(source.find("menuPopDialog(") == std::string::npos);
    }
}

TEST_CASE("menu diagnostics overlay does not execute legacy dynamic title callbacks", "[menus][debug][static][b359]")
{
    const std::string bridge = readTextFile("port/fast3d/pdgui_bridge.c");
    const std::string mainmenu = readTextFile("src/game/mainmenu.c");

    REQUIRE_FALSE(bridge.empty());
    REQUIRE_FALSE(mainmenu.empty());

    const std::string debugFormatter = functionBlock(bridge, "pdguiDebugFormatLegacyMenuInfo");
    const std::string debugResolver = functionBlock(bridge, "pdguiDebugResolveLegacyDialogTitle");
    const std::string pauseTitle = functionBlock(mainmenu, "soloMenuTitlePauseStatus");

    REQUIRE_FALSE(debugFormatter.empty());
    REQUIRE_FALSE(debugResolver.empty());
    REQUIRE_FALSE(pauseTitle.empty());

    REQUIRE(debugFormatter.find("menuResolveDialogTitle(") == std::string::npos);
    REQUIRE(debugFormatter.find("pdguiDebugResolveLegacyDialogTitle(") != std::string::npos);
    REQUIRE(debugResolver.find("\"<dynamic title>\"") != std::string::npos);
    REQUIRE(debugResolver.find("return handler(") == std::string::npos);
    REQUIRE(pauseTitle.find("!curdialog || !curdialog->definition") != std::string::npos);
}

TEST_CASE("main menu entries wait for the CI camera before opening input", "[input][menu_graph][mainmenu][static]")
{
    const std::string menutick = readTextFile("src/game/menutick.c");

    REQUIRE_FALSE(menutick.empty());

    const std::string ready = functionBlock(menutick, "ciReadyForMenuOpen");
    const std::string hold = functionBlock(menutick, "ciHoldMenuOpenUntilCameraReady");
    const std::string tick = functionBlock(menutick, "menuTick");

    REQUIRE_FALSE(ready.empty());
    REQUIRE_FALSE(hold.empty());
    REQUIRE_FALSE(tick.empty());

    REQUIRE(ready.find("g_Vars.stagenum != STAGE_CITRAINING") != std::string::npos);
    REQUIRE(ready.find("g_Vars.lvframenum < 4") != std::string::npos);
    REQUIRE(ready.find("g_Vars.tickmode == TICKMODE_CUTSCENE") != std::string::npos);
    REQUIRE(ready.find("!playerCurrentCutsceneInProgress()") != std::string::npos);
    REQUIRE(ready.find("playerEndCutscene()") != std::string::npos);
    REQUIRE(hold.find("g_PlayersWithControl[0] = false") != std::string::npos);

    REQUIRE(tick.find("var80087260 > 0") != std::string::npos);
    REQUIRE(tick.find("g_PostExitMainMenuView >= 0") != std::string::npos);
    REQUIRE(tick.find("g_FileState == FILESTATE_UNSELECTED") != std::string::npos);
    REQUIRE(findAll(tick, "ciReadyForMenuOpen()").size() >= 3);
    REQUIRE(findAll(tick, "ciHoldMenuOpenUntilCameraReady()").size() >= 2);
    REQUIRE(tick.find("g_Vars.lvframenum > 300") == std::string::npos);
}
