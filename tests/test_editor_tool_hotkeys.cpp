/*
 * test_editor_tool_hotkeys.cpp -- static guards for editor/tool action-map hotkeys.
 *
 * @SYNC port/include/actionmap.h
 * @SYNC port/include/input.h
 * @SYNC port/src/actionmap.cpp
 * @SYNC port/fast3d/pdgui_forge_hud.cpp
 * @SYNC port/fast3d/pdgui_forge_editor.cpp
 * @SYNC port/fast3d/pdgui_skin_editor.cpp
 */

#include "catch.hpp"

#include <fstream>
#include <sstream>
#include <string>

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

} /* namespace */

TEST_CASE("editor hotkeys: Forge commands use action-map actions", "[input][forge][editor][static]")
{
    const std::string header = readTextFile("port/include/actionmap.h");
    const std::string actionmap = readTextFile("port/src/actionmap.cpp");
    const std::string inputlayer = readTextFile("port/src/inputlayer.c");
    const std::string hud = readTextFile("port/fast3d/pdgui_forge_hud.cpp");
    const std::string editor = readTextFile("port/fast3d/pdgui_forge_editor.cpp");

    REQUIRE_FALSE(header.empty());
    REQUIRE_FALSE(actionmap.empty());
    REQUIRE_FALSE(inputlayer.empty());
    REQUIRE_FALSE(hud.empty());
    REQUIRE_FALSE(editor.empty());

    REQUIRE(header.find("ACTION_FORGE_PLACE_CANCEL") != std::string::npos);
    REQUIRE(header.find("ACTION_FORGE_BOT_ADD") != std::string::npos);
    REQUIRE(actionmap.find("VK_CHORD_CTRL_TAB") != std::string::npos);
    REQUIRE(actionmap.find("addBind(imc, ACTION_FORGE_TAB_NEXT,         VK_CHORD_CTRL_TAB)") != std::string::npos);
    REQUIRE(actionmap.find("addBind(imc, ACTION_FORGE_BOT_ADD") != std::string::npos);
    REQUIRE(inputlayer.find("ACTION_FORGE_BOT_SPAWN_CYCLE") != std::string::npos);

    REQUIRE(hud.find("actionPressed(0, ACTION_FORGE_BOT_ADD)") != std::string::npos);
    REQUIRE(hud.find("actionPressed(0, ACTION_FORGE_BOT_REMOVE_ALL)") != std::string::npos);
    REQUIRE(hud.find("actionPressed(0, ACTION_FORGE_BOT_FREEZE_TOGGLE)") != std::string::npos);
    REQUIRE(hud.find("actionPressed(0, ACTION_FORGE_BOT_SPAWN_CYCLE)") != std::string::npos);
    REQUIRE(hud.find("ImGui::IsKeyPressed(") == std::string::npos);

    REQUIRE(editor.find("actionPressed(0, ACTION_FORGE_PLACE_CANCEL)") != std::string::npos);
    REQUIRE(editor.find("ImGui::IsKeyPressed(") == std::string::npos);
}

TEST_CASE("editor hotkeys: Skin Editor commands use action-map actions", "[input][skin][editor][static]")
{
    const std::string header = readTextFile("port/include/actionmap.h");
    const std::string inputHeader = readTextFile("port/include/input.h");
    const std::string actionmap = readTextFile("port/src/actionmap.cpp");
    const std::string skin = readTextFile("port/fast3d/pdgui_skin_editor.cpp");
    const std::string mainmenu = readTextFile("port/fast3d/pdgui_menu_mainmenu.cpp");

    REQUIRE_FALSE(header.empty());
    REQUIRE_FALSE(inputHeader.empty());
    REQUIRE_FALSE(actionmap.empty());
    REQUIRE_FALSE(skin.empty());
    REQUIRE_FALSE(mainmenu.empty());

    REQUIRE(header.find("ACTION_SKIN_BRUSH_DECREASE") != std::string::npos);
    REQUIRE(header.find("ACTION_SKIN_SAVE") != std::string::npos);
    REQUIRE(inputHeader.find("VK_CHORD_CTRL_S") != std::string::npos);
    REQUIRE(actionmap.find("addBind(imc, ACTION_SKIN_UNDO,            VK_CHORD_CTRL_Z)") != std::string::npos);
    REQUIRE(actionmap.find("addBind(imc, ACTION_SKIN_REDO,            VK_CHORD_CTRL_SHIFT_Z)") != std::string::npos);
    REQUIRE(actionmap.find("addBind(imc, ACTION_SKIN_SAVE,            VK_CHORD_CTRL_S)") != std::string::npos);

    REQUIRE(skin.find("actionPressed(0, ACTION_SKIN_BRUSH_DECREASE)") != std::string::npos);
    REQUIRE(skin.find("actionPressed(0, ACTION_SKIN_TOOL_DRAW)") != std::string::npos);
    REQUIRE(skin.find("actionPressed(0, ACTION_SKIN_GRID_TOGGLE)") != std::string::npos);
    REQUIRE(skin.find("actionPressed(0, ACTION_SKIN_UNDO)") != std::string::npos);
    REQUIRE(skin.find("actionPressed(0, ACTION_SKIN_SAVE)") != std::string::npos);
    REQUIRE(skin.find("ImGui::IsKeyPressed(") == std::string::npos);
    REQUIRE(skin.find("ImGui::GetIO().KeyCtrl") == std::string::npos);

    REQUIRE(mainmenu.find("BG_SKIN_EDITOR") != std::string::npos);
    REQUIRE(mainmenu.find("ACTION_SKIN_SAVE") != std::string::npos);
}
