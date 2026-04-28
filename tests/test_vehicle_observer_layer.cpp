/*
 * test_vehicle_observer_layer.cpp -- production wiring guards for
 * vehicle and observer scene-layer migration.
 *
 * @SYNC src/game/bondbike.c
 * @SYNC src/game/forgemode.c
 * @SYNC port/src/spectator.c
 * @SYNC port/src/inputlayer.c
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

} /* namespace */

TEST_CASE("vehicle layer wiring: hoverbike routes through scene layer", "[input][vehicle][static]")
{
    const std::string inputlayer = readTextFile("port/src/inputlayer.c");
    const std::string bondbike = readTextFile("src/game/bondbike.c");

    REQUIRE_FALSE(inputlayer.empty());
    REQUIRE_FALSE(bondbike.empty());

    REQUIRE(inputlayer.find("s_VehicleDriverActionSet") != std::string::npos);
    REQUIRE(inputlayer.find("ACTION_VEHICLE_ACCELERATE") != std::string::npos);
    REQUIRE(inputlayer.find("ACTION_VEHICLE_EXIT") != std::string::npos);

    const std::string vehiclePush = functionBlock(inputlayer, "onVehicleDriverPush");
    REQUIRE_FALSE(vehiclePush.empty());
    REQUIRE(vehiclePush.find("imcVehicleMount()") != std::string::npos);
    REQUIRE(vehiclePush.find("actionmapFlushGameplayState()") != std::string::npos);
    REQUIRE(vehiclePush.find("actionmapFlushActionSet(s_VehicleDriverActionSet") != std::string::npos);

    const std::string vehiclePop = functionBlock(inputlayer, "onVehicleDriverPop");
    REQUIRE_FALSE(vehiclePop.empty());
    REQUIRE(vehiclePop.find("imcVehicleDismount()") != std::string::npos);
    REQUIRE(vehiclePop.find("actionmapFlushActionSet(s_VehicleDriverActionSet") != std::string::npos);

    REQUIRE(inputlayer.find(".action_set            = s_VehicleDriverActionSet") != std::string::npos);
    REQUIRE(inputlayer.find(".on_push               = onVehicleDriverPush") != std::string::npos);
    REQUIRE(inputlayer.find(".on_pop                = onVehicleDriverPop") != std::string::npos);
    REQUIRE(inputlayer.find(".on_abort              = onVehicleDriverAbort") != std::string::npos);
    REQUIRE(inputlayer.find(".imc                   = &g_ImcVehicle") != std::string::npos);

    const std::string init = functionBlock(bondbike, "bbikeInit");
    const std::string exit = functionBlock(bondbike, "bbikeExit");
    REQUIRE_FALSE(init.empty());
    REQUIRE_FALSE(exit.empty());
    REQUIRE(init.find("sceneFire(SCENE_EVENT_VEHICLE_BOARD") != std::string::npos);
    REQUIRE(init.find("imcVehicleMount()") == std::string::npos);
    REQUIRE(exit.find("sceneFire(SCENE_EVENT_VEHICLE_DISMOUNT") != std::string::npos);
    REQUIRE(exit.find("imcVehicleDismount()") == std::string::npos);
}

TEST_CASE("observer layer wiring: forge and spectator fire observer events", "[input][observer][static]")
{
    const std::string inputlayer = readTextFile("port/src/inputlayer.c");
    const std::string sceneHeader = readTextFile("port/include/scene.h");
    const std::string scene = readTextFile("port/src/scene.c");
    const std::string forge = readTextFile("src/game/forgemode.c");
    const std::string spectator = readTextFile("port/src/spectator.c");

    REQUIRE_FALSE(inputlayer.empty());
    REQUIRE_FALSE(sceneHeader.empty());
    REQUIRE_FALSE(scene.empty());
    REQUIRE_FALSE(forge.empty());
    REQUIRE_FALSE(spectator.empty());

    REQUIRE(sceneHeader.find("#define SCENE_OBSERVER_SOURCE_SPECTATOR  3") != std::string::npos);

    REQUIRE(inputlayer.find("s_ObserverActionSet") != std::string::npos);
    REQUIRE(inputlayer.find("ACTION_FORGE_TOGGLE") != std::string::npos);
    REQUIRE(inputlayer.find("ACTION_FORGE_TAB_NEXT") != std::string::npos);
    REQUIRE(inputlayer.find("ACTION_OBSERVER_SUBSET_PREV") != std::string::npos);
    REQUIRE(inputlayer.find("ACTION_OBSERVER_STOP") != std::string::npos);
    REQUIRE(inputlayer.find(".action_set            = s_ObserverActionSet") != std::string::npos);
    REQUIRE(inputlayer.find(".on_push               = onObserverPush") != std::string::npos);
    REQUIRE(inputlayer.find(".on_pop                = onObserverPop") != std::string::npos);
    REQUIRE(inputlayer.find(".on_abort              = onObserverAbort") != std::string::npos);
    REQUIRE(inputlayer.find(".imc                   = &g_ImcObserver") != std::string::npos);

    const std::string observerPop = functionBlock(inputlayer, "onObserverPop");
    const std::string observerPush = functionBlock(inputlayer, "onObserverPush");
    REQUIRE_FALSE(observerPush.empty());
    REQUIRE_FALSE(observerPop.empty());
    REQUIRE(observerPush.find("SCENE_OBSERVER_SOURCE_SPECTATOR") != std::string::npos);
    REQUIRE(observerPush.find("imcActivate(&g_ImcObserver)") != std::string::npos);
    REQUIRE(observerPop.find("actionmapFlushActionSet(s_ObserverActionSet") != std::string::npos);
    REQUIRE(observerPop.find("imcDeactivate(&g_ImcObserver)") != std::string::npos);
    REQUIRE(observerPop.find("imcDeactivate(&g_ImcForge)") != std::string::npos);
    REQUIRE(observerPop.find("imcDeactivate(&g_ImcForgeSession)") != std::string::npos);
    REQUIRE(scene.find("s_ObserverPayload.source = s_ObserverSource") != std::string::npos);
    REQUIRE(scene.find("inputLayerPush(&g_LayerObserver, &s_ObserverPayload)") != std::string::npos);

    const std::string forgeEnter = functionBlock(forge, "forgeObserverLayerEnter");
    const std::string forgeExit = functionBlock(forge, "forgeObserverLayerExit");
    REQUIRE_FALSE(forgeEnter.empty());
    REQUIRE_FALSE(forgeExit.empty());
    REQUIRE(forgeEnter.find("SCENE_OBSERVER_SOURCE_FORGE") != std::string::npos);
    REQUIRE(forgeEnter.find("sceneFire(SCENE_EVENT_OBSERVER_ENTER") != std::string::npos);
    REQUIRE(forgeExit.find("sceneFire(SCENE_EVENT_OBSERVER_EXIT") != std::string::npos);
    REQUIRE(forge.find("forgeObserverLayerEnter(reason)") != std::string::npos);
    REQUIRE(forge.find("forgeObserverLayerExit(reason)") != std::string::npos);

    const std::string specEnter = functionBlock(spectator, "spectatorObserverLayerEnter");
    const std::string specExit = functionBlock(spectator, "spectatorObserverLayerExit");
    REQUIRE_FALSE(specEnter.empty());
    REQUIRE_FALSE(specExit.empty());
    REQUIRE(specEnter.find("SCENE_OBSERVER_SOURCE_SPECTATOR") != std::string::npos);
    REQUIRE(specEnter.find("sceneFire(SCENE_EVENT_OBSERVER_ENTER") != std::string::npos);
    REQUIRE(specExit.find("sceneFire(SCENE_EVENT_OBSERVER_EXIT") != std::string::npos);

    const std::string beginLive = functionBlock(spectator, "spectatorBeginLive");
    const std::string beginTheater = functionBlock(spectator, "spectatorBeginTheater");
    const std::string stop = functionBlock(spectator, "void spectatorStop");
    REQUIRE_FALSE(beginLive.empty());
    REQUIRE_FALSE(beginTheater.empty());
    REQUIRE_FALSE(stop.empty());
    REQUIRE(beginLive.find("spectatorObserverLayerEnter()") != std::string::npos);
    REQUIRE(beginTheater.find("spectatorObserverLayerEnter()") != std::string::npos);
    REQUIRE(stop.find("spectatorObserverLayerExit()") != std::string::npos);
}

TEST_CASE("observer action map: spectator overlay uses observer actions", "[input][observer][spectator][static]")
{
    const std::string actionHeader = readTextFile("port/include/actionmap.h");
    const std::string actionmap = readTextFile("port/src/actionmap.cpp");
    const std::string spectator = readTextFile("port/fast3d/pdgui_spectator.cpp");
    const std::string glyphs = readTextFile("port/fast3d/pdgui_glyphs.cpp");
    const std::string mainmenu = readTextFile("port/fast3d/pdgui_menu_mainmenu.cpp");

    REQUIRE_FALSE(actionHeader.empty());
    REQUIRE_FALSE(actionmap.empty());
    REQUIRE_FALSE(spectator.empty());
    REQUIRE_FALSE(glyphs.empty());
    REQUIRE_FALSE(mainmenu.empty());

    REQUIRE(actionHeader.find("ACTION_OBSERVER_SUBSET_PREV") != std::string::npos);
    REQUIRE(actionHeader.find("ACTION_OBSERVER_ASCEND") != std::string::npos);
    REQUIRE(actionHeader.find("extern InputMappingContext g_ImcObserver") != std::string::npos);
    REQUIRE(actionmap.find("InputMappingContext g_ImcObserver") != std::string::npos);
    REQUIRE(actionmap.find("setupObserverDefaults") != std::string::npos);
    REQUIRE(actionmap.find("addBind(imc, ACTION_OBSERVER_SUBSET_PREV,   VKL_PAGEUP)") != std::string::npos);
    REQUIRE(actionmap.find("addBind(imc, ACTION_OBSERVER_FREEFLY,       VKL_R)") != std::string::npos);
    REQUIRE(actionmap.find("addBind(imc, ACTION_OBSERVER_STOP,          VK_ESCAPE)") != std::string::npos);
    REQUIRE(actionmap.find("ctx == &g_ImcObserver") != std::string::npos);

    REQUIRE(spectator.find("#include \"actionmap.h\"") != std::string::npos);
    REQUIRE(spectator.find("actionPressed(0, ACTION_OBSERVER_SUBSET_PREV)") != std::string::npos);
    REQUIRE(spectator.find("actionPressed(0, ACTION_OBSERVER_CAMERA_TOGGLE)") != std::string::npos);
    REQUIRE(spectator.find("actionReleased(0, ACTION_OBSERVER_FREEFLY)") != std::string::npos);
    REQUIRE(spectator.find("actionAxis(0, ACTION_AXIS_MOVE_X") != std::string::npos);
    REQUIRE(spectator.find("actionHeld(0, ACTION_OBSERVER_ASCEND)") != std::string::npos);
    REQUIRE(spectator.find("ImGui::IsKeyPressed") == std::string::npos);
    REQUIRE(spectator.find("ImGui::IsKeyReleased") == std::string::npos);
    REQUIRE(spectator.find("ImGui::IsKeyDown") == std::string::npos);

    REQUIRE(glyphs.find("&g_ImcObserver") != std::string::npos);
    REQUIRE(mainmenu.find("BG_OBSERVER") != std::string::npos);
    REQUIRE(mainmenu.find("&g_ImcObserver") != std::string::npos);
}
