/*
 * test_scene_dispatch.cpp -- Cohort 3 invariants for the Scene Manager.
 *
 * Asserts that every SceneEvent translates into the documented
 * inputLayer push/pop/abort sequence, including idempotency for
 * already-applied events and graceful handling of out-of-order pairs.
 *
 * Cohort 4 (cutscene flash fix) and Cohort 5 (vehicle / observer
 * wiring) will land on top of this dispatcher; this test suite is
 * the contract those cohorts cannot break.
 *
 * @SYNC port/src/scene.c
 * @SYNC context/designs/input-universality-and-transitions-2026-04-27.md SD
 *
 * Logging channel reserved for runtime diagnostics: TRANSITION.SCENE
 */

#include "catch.hpp"

#include <fstream>
#include <sstream>
#include <string>

extern "C" {
#include "scene_pure.h"
#include "inputlayer_pure.h"
}

namespace {

void resetWorld()
{
    spShutdown();
    ilpShutdown();
    ilpInstrumentReset(nullptr);
    spInstrumentReset(nullptr);
    ilpInit();
    spInit();
}

std::string readTextFile(const char *path)
{
    std::ifstream in(path, std::ios::binary);
    REQUIRE(in.good());
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

} /* namespace */

TEST_CASE("scene: BOOT_COMPLETE replaces BOOT with GAMEPLAY", "[scene][boot]")
{
    resetWorld();
    REQUIRE(ilpTopType() == ILP_LAYER_BOOT);
    REQUIRE(ilpDepth()   == 1);

    REQUIRE(spFire(SP_SCENE_EVENT_BOOT_COMPLETE, nullptr) == 0);
    REQUIRE(ilpTopType() == ILP_LAYER_GAMEPLAY);
    REQUIRE(ilpDepth()   == 2);
    REQUIRE(ilpHas(ILP_LAYER_BOOT) == 1); /* BOOT remains as root */
}

TEST_CASE("scene: STAGE_READY pushes GAMEPLAY when not already top", "[scene][stage]")
{
    resetWorld();
    /* Start on BOOT only; STAGE_READY should push GAMEPLAY. */
    REQUIRE(spFire(SP_SCENE_EVENT_STAGE_READY, nullptr) == 0);
    REQUIRE(ilpTopType() == ILP_LAYER_GAMEPLAY);
}

TEST_CASE("scene: STAGE_READY is idempotent when already on GAMEPLAY", "[scene][stage]")
{
    resetWorld();
    spFire(SP_SCENE_EVENT_STAGE_READY, nullptr);
    int before_depth = ilpDepth();
    REQUIRE(spFire(SP_SCENE_EVENT_STAGE_READY, nullptr) == 0);
    REQUIRE(ilpDepth() == before_depth); /* no extra push */
    REQUIRE(ilpTopType() == ILP_LAYER_GAMEPLAY);
}

TEST_CASE("scene: STAGE_LOADING is advisory (no state change)", "[scene][stage]")
{
    resetWorld();
    int before_depth = ilpDepth();
    REQUIRE(spFire(SP_SCENE_EVENT_STAGE_LOADING, nullptr) == 0);
    REQUIRE(ilpDepth() == before_depth);
    REQUIRE(ilpTopType() == ILP_LAYER_BOOT);
}

TEST_CASE("scene: CUTSCENE_START + CUTSCENE_END round-trip", "[scene][cutscene]")
{
    resetWorld();
    spFire(SP_SCENE_EVENT_STAGE_READY, nullptr);
    REQUIRE(ilpTopType() == ILP_LAYER_GAMEPLAY);

    REQUIRE(spFire(SP_SCENE_EVENT_CUTSCENE_START, nullptr) == 0);
    REQUIRE(ilpTopType() == ILP_LAYER_CUTSCENE);
    REQUIRE(ilpHas(ILP_LAYER_GAMEPLAY) == 1); /* GAMEPLAY stays beneath */

    REQUIRE(spFire(SP_SCENE_EVENT_CUTSCENE_END, nullptr) == 0);
    REQUIRE(ilpTopType() == ILP_LAYER_GAMEPLAY);
    REQUIRE(ilpHas(ILP_LAYER_CUTSCENE) == 0);
}

TEST_CASE("scene: duplicate CUTSCENE_START is idempotent (no second push)", "[scene][cutscene][idempotent]")
{
    resetWorld();
    spFire(SP_SCENE_EVENT_STAGE_READY, nullptr);
    spFire(SP_SCENE_EVENT_CUTSCENE_START, nullptr);

    int before_depth = ilpDepth();
    REQUIRE(spFire(SP_SCENE_EVENT_CUTSCENE_START, nullptr) == 0);
    REQUIRE(ilpDepth() == before_depth);
    REQUIRE(ilpTopType() == ILP_LAYER_CUTSCENE);
}

TEST_CASE("scene: CUTSCENE_END without START is idempotent (no error)", "[scene][cutscene][idempotent]")
{
    resetWorld();
    spFire(SP_SCENE_EVENT_STAGE_READY, nullptr);
    int before_depth = ilpDepth();
    REQUIRE(spFire(SP_SCENE_EVENT_CUTSCENE_END, nullptr) == 0);
    REQUIRE(ilpDepth() == before_depth);
}

TEST_CASE("scene: PAUSE_OPEN + PAUSE_CLOSE round-trip", "[scene][pause]")
{
    resetWorld();
    spFire(SP_SCENE_EVENT_STAGE_READY, nullptr);

    REQUIRE(spFire(SP_SCENE_EVENT_PAUSE_OPEN, nullptr) == 0);
    REQUIRE(ilpTopType() == ILP_LAYER_MENU);

    REQUIRE(spFire(SP_SCENE_EVENT_PAUSE_CLOSE, nullptr) == 0);
    REQUIRE(ilpTopType() == ILP_LAYER_GAMEPLAY);
}

TEST_CASE("scene: VEHICLE_BOARD/DISMOUNT for driver + turret", "[scene][vehicle]")
{
    resetWorld();
    spFire(SP_SCENE_EVENT_STAGE_READY, nullptr);

    SECTION("driver default") {
        REQUIRE(spFire(SP_SCENE_EVENT_VEHICLE_BOARD, nullptr) == 0);
        REQUIRE(ilpTopType() == ILP_LAYER_VEHICLE_DRIVER);
        REQUIRE(spFire(SP_SCENE_EVENT_VEHICLE_DISMOUNT, nullptr) == 0);
        REQUIRE(ilpTopType() == ILP_LAYER_GAMEPLAY);
    }
    SECTION("explicit driver") {
        SpSceneVehiclePayload p = { SP_VEHICLE_KIND_DRIVER };
        REQUIRE(spFire(SP_SCENE_EVENT_VEHICLE_BOARD, &p) == 0);
        REQUIRE(ilpTopType() == ILP_LAYER_VEHICLE_DRIVER);
    }
    SECTION("turret") {
        SpSceneVehiclePayload p = { SP_VEHICLE_KIND_TURRET };
        REQUIRE(spFire(SP_SCENE_EVENT_VEHICLE_BOARD, &p) == 0);
        REQUIRE(ilpTopType() == ILP_LAYER_VEHICLE_TURRET);
    }
}

TEST_CASE("scene: OBSERVER_ENTER/EXIT round-trip", "[scene][observer]")
{
    resetWorld();
    spFire(SP_SCENE_EVENT_STAGE_READY, nullptr);

    REQUIRE(spFire(SP_SCENE_EVENT_OBSERVER_ENTER, nullptr) == 0);
    REQUIRE(ilpTopType() == ILP_LAYER_OBSERVER);

    REQUIRE(spFire(SP_SCENE_EVENT_OBSERVER_EXIT, nullptr) == 0);
    REQUIRE(ilpTopType() == ILP_LAYER_GAMEPLAY);
}

TEST_CASE("scene: STAGE_TEARDOWN unwinds everything to BOOT", "[scene][teardown]")
{
    resetWorld();
    spFire(SP_SCENE_EVENT_STAGE_READY,    nullptr);
    spFire(SP_SCENE_EVENT_CUTSCENE_START, nullptr);
    spFire(SP_SCENE_EVENT_PAUSE_OPEN,     nullptr);
    REQUIRE(ilpDepth() >= 4);

    REQUIRE(spFire(SP_SCENE_EVENT_STAGE_TEARDOWN, nullptr) == 0);
    REQUIRE(ilpTopType() == ILP_LAYER_BOOT);
    REQUIRE(ilpDepth()   == 1);
    /* All cached handles cleared so a fresh CUTSCENE_START works. */
    spFire(SP_SCENE_EVENT_STAGE_READY, nullptr);
    REQUIRE(spFire(SP_SCENE_EVENT_CUTSCENE_START, nullptr) == 0);
    REQUIRE(ilpTopType() == ILP_LAYER_CUTSCENE);
}

TEST_CASE("scene: DISCONNECT unwinds everything to BOOT", "[scene][disconnect]")
{
    resetWorld();
    spFire(SP_SCENE_EVENT_STAGE_READY, nullptr);
    spFire(SP_SCENE_EVENT_PAUSE_OPEN,  nullptr);

    REQUIRE(spFire(SP_SCENE_EVENT_DISCONNECT, nullptr) == 0);
    REQUIRE(ilpTopType() == ILP_LAYER_BOOT);
}

TEST_CASE("scene: nested layers preserve ancestry", "[scene][nesting]")
{
    /* GAMEPLAY -> CUTSCENE -> PAUSE: pop-PAUSE returns to CUTSCENE; pop-CUTSCENE returns to GAMEPLAY. */
    resetWorld();
    spFire(SP_SCENE_EVENT_STAGE_READY,    nullptr);
    spFire(SP_SCENE_EVENT_CUTSCENE_START, nullptr);
    spFire(SP_SCENE_EVENT_PAUSE_OPEN,     nullptr);
    REQUIRE(ilpTopType() == ILP_LAYER_MENU);

    spFire(SP_SCENE_EVENT_PAUSE_CLOSE, nullptr);
    REQUIRE(ilpTopType() == ILP_LAYER_CUTSCENE);

    spFire(SP_SCENE_EVENT_CUTSCENE_END, nullptr);
    REQUIRE(ilpTopType() == ILP_LAYER_GAMEPLAY);
}

TEST_CASE("scene: tracked close unwinds layers above the target", "[scene][nesting][teardown]")
{
    resetWorld();
    spFire(SP_SCENE_EVENT_STAGE_READY,    nullptr);
    spFire(SP_SCENE_EVENT_CUTSCENE_START, nullptr);
    spFire(SP_SCENE_EVENT_PAUSE_OPEN,     nullptr);
    REQUIRE(ilpTopType() == ILP_LAYER_MENU);

    REQUIRE(spFire(SP_SCENE_EVENT_CUTSCENE_END, nullptr) == 0);
    REQUIRE(ilpTopType() == ILP_LAYER_GAMEPLAY);
    REQUIRE(ilpHas(ILP_LAYER_MENU) == 0);
    REQUIRE(ilpHas(ILP_LAYER_CUTSCENE) == 0);

    REQUIRE(spFire(SP_SCENE_EVENT_PAUSE_OPEN, nullptr) == 0);
    REQUIRE(ilpTopType() == ILP_LAYER_MENU);
}

TEST_CASE("scene: out-of-range event id rejected", "[scene][safety]")
{
    resetWorld();
    REQUIRE(spFire((SpSceneEvent)-1, nullptr) == -1);
    REQUIRE(spFire((SpSceneEvent)SP_SCENE_EVENT_COUNT, nullptr) == -1);
    REQUIRE(spFire((SpSceneEvent)9999, nullptr) == -1);
}

TEST_CASE("scene: instrumentation tallies fire / reject counts", "[scene][instrument]")
{
    resetWorld();
    spInstrumentReset(nullptr);

    spFire(SP_SCENE_EVENT_STAGE_READY,    nullptr);
    spFire(SP_SCENE_EVENT_CUTSCENE_START, nullptr);
    spFire(SP_SCENE_EVENT_CUTSCENE_START, nullptr); /* idempotent, still counted */
    spFire(SP_SCENE_EVENT_CUTSCENE_END,   nullptr);

    const SpFireCounts *c = spInstrumentGet();
    REQUIRE(c->fire_count[SP_SCENE_EVENT_STAGE_READY]    == 1);
    REQUIRE(c->fire_count[SP_SCENE_EVENT_CUTSCENE_START] == 2);
    REQUIRE(c->fire_count[SP_SCENE_EVENT_CUTSCENE_END]   == 1);
    REQUIRE(c->reject_count[SP_SCENE_EVENT_CUTSCENE_START] == 0);
}

TEST_CASE("scene: sceneCurrentLayer mirrors inputLayer top type", "[scene][query]")
{
    resetWorld();
    REQUIRE(spCurrentLayer() == ILP_LAYER_BOOT);
    spFire(SP_SCENE_EVENT_STAGE_READY, nullptr);
    REQUIRE(spCurrentLayer() == ILP_LAYER_GAMEPLAY);
    spFire(SP_SCENE_EVENT_CUTSCENE_START, nullptr);
    REQUIRE(spCurrentLayer() == ILP_LAYER_CUTSCENE);
}

TEST_CASE("scene transition helper: centralizes manifest/menu cleanup before stage change",
          "[scene][transition][static]")
{
    const std::string header = readTextFile("port/include/scene_transition.h");
    const std::string impl = readTextFile("port/src/scene_transition.c");
    const std::string cmake = readTextFile("CMakeLists.txt");

    REQUIRE(header.find("SCENE_STAGE_TRANSITION_CLEAR_CLIENT_MANIFEST") != std::string::npos);
    REQUIRE(header.find("SCENE_STAGE_TRANSITION_RELEASE_MENU_POOL") != std::string::npos);
    REQUIRE(header.find("SCENE_STAGE_TRANSITION_DISCONNECT") != std::string::npos);
    REQUIRE(header.find("sceneStageTransitionPrepare(u32 flags, const char *reason)") != std::string::npos);
    REQUIRE(header.find("sceneStageChangeTo(s32 stagenum, u32 flags, const char *reason)") != std::string::npos);

    const size_t clear = impl.find("manifestClear(&g_ClientManifest);");
    const size_t change = impl.find("mainChangeToStage(stagenum);");
    REQUIRE(clear != std::string::npos);
    REQUIRE(change != std::string::npos);
    REQUIRE(clear < change);

    REQUIRE(impl.find("sceneFire(SCENE_EVENT_DISCONNECT, NULL);") != std::string::npos);
    REQUIRE(impl.find("menupoolReleaseAll();") != std::string::npos);
    REQUIRE(cmake.find("port/src/scene_transition.c") != std::string::npos);
}

TEST_CASE("scene transition helper: priority transition sites use shared cleanup",
          "[scene][transition][static]")
{
    const std::string bridge = readTextFile("port/fast3d/pdgui_bridge.c");
    const std::string net = readTextFile("port/src/net/net.c");
    const std::string netmsg = readTextFile("port/src/net/netmsg.c");
    const std::string match = readTextFile("port/src/net/matchsetup.c");
    const std::string menutick = readTextFile("src/game/menutick.c");

    REQUIRE(bridge.find("#include \"scene_transition.h\"") != std::string::npos);
    REQUIRE(bridge.find("\"endscreen retry\"") != std::string::npos);
    REQUIRE(bridge.find("\"endscreen next mission\"") != std::string::npos);
    REQUIRE(bridge.find("\"endscreen exit to main menu\"") != std::string::npos);
    REQUIRE(bridge.find("manifestClear(&g_ClientManifest);") == std::string::npos);
    REQUIRE(bridge.find("menupoolReleaseAll();") == std::string::npos);

    REQUIRE(net.find("SCENE_STAGE_TRANSITION_DISCONNECT") != std::string::npos);
    REQUIRE(net.find("\"netDisconnect lobby return\"") != std::string::npos);
    REQUIRE(net.find("sceneStageChangeTo(STAGE_CITRAINING") != std::string::npos);

    REQUIRE(netmsg.find("\"SVC_STAGE_START coop\"") != std::string::npos);
    REQUIRE(netmsg.find("\"SVC_STAGE_START combat\"") != std::string::npos);
    const size_t svc_stage_end = netmsg.find("u32 netmsgSvcStageEndRead");
    const size_t svc_stage_end_clear = netmsg.find(
        "sceneStageTransitionPrepare(SCENE_STAGE_TRANSITION_CLEAR_CLIENT_MANIFEST",
        svc_stage_end);
    REQUIRE(svc_stage_end != std::string::npos);
    REQUIRE(svc_stage_end_clear != std::string::npos);
    REQUIRE(svc_stage_end < svc_stage_end_clear);
    REQUIRE(netmsg.find("menupoolReleaseAll();") == std::string::npos);

    REQUIRE(match.find("\"matchStart\"") != std::string::npos);
    REQUIRE(match.find("\"matchStartFromChallenge\"") != std::string::npos);
    REQUIRE(match.find("menupoolReleaseAll();") == std::string::npos);

    REQUIRE(menutick.find("\"menutick deep sea auto advance\"") != std::string::npos);
    REQUIRE(menutick.find("\"menutick MPENDSCREEN restart\"") != std::string::npos);
    REQUIRE(menutick.find("\"menutick MPENDSCREEN exit\"") != std::string::npos);
    REQUIRE(menutick.find("\"menutick COOPCONTINUE exit\"") != std::string::npos);
    REQUIRE(menutick.find("manifestClear(&g_ClientManifest);") == std::string::npos);
}

TEST_CASE("offline Combat Simulator starts with a predeclared MP manifest",
          "[scene][manifest][combat-sim][static]")
{
    const std::string mplayer = readTextFile("src/game/mplayer/mplayer.c");
    const std::string manifest = readTextFile("port/src/net/netmanifest.c");

    REQUIRE(mplayer.find("#include \"net/netmanifest.h\"") != std::string::npos);

    const size_t localBranch = mplayer.find("if (g_NetMode == NETMODE_NONE)");
    const size_t build = mplayer.find("manifestBuildForHost(&g_ClientManifest);", localBranch);
    const size_t mpFlag = mplayer.find("g_Vars.normmplayerisrunning = true;", build);
    const size_t change = mplayer.find("mainChangeToStage(stagenum);", mpFlag);
    REQUIRE(localBranch != std::string::npos);
    REQUIRE(build != std::string::npos);
    REQUIRE(mpFlag != std::string::npos);
    REQUIRE(change != std::string::npos);
    REQUIRE(localBranch < build);
    REQUIRE(build < mpFlag);
    REQUIRE(mpFlag < change);

    REQUIRE(manifest.find("Offline Combat Simulator has no net-local client") !=
            std::string::npos);
    REQUIRE(manifest.find("sl->type != SLOT_PLAYER") != std::string::npos);
    REQUIRE(manifest.find("s_manifestExpandDeps(out, be->id, slot_index)") !=
            std::string::npos);
    REQUIRE(manifest.find("s_manifestExpandDeps(out, he->id, slot_index)") !=
            std::string::npos);
}

TEST_CASE("Combat Simulator bot target commands validate live prop backlinks",
          "[combat-sim][bot][static]")
{
    const std::string bot = readTextFile("src/game/bot.c");

    REQUIRE(bot.find("botLiveChrPropFromPropnum") != std::string::npos);
    REQUIRE(bot.find("botLiveChrFromPropnum") != std::string::npos);
    REQUIRE(bot.find("botLiveMpIndexFromPropnum") != std::string::npos);
    REQUIRE(bot.find("botLiveChrPropnum") != std::string::npos);
    REQUIRE(bot.find("botClearStaleHumanCommand") != std::string::npos);
    REQUIRE(bot.find("prop->chr->prop != prop") != std::string::npos);
    REQUIRE(bot.find("botLiveChrFromPropnum(chr->aibot->attackpropnum)") !=
            std::string::npos);
    REQUIRE(bot.find("botLiveMpIndexFromPropnum(aibot->followprotectpropnum)") !=
            std::string::npos);

    REQUIRE(bot.find("&g_Vars.props[chr->aibot->attackpropnum]") ==
            std::string::npos);
    REQUIRE(bot.find("g_Vars.props + botchr->target") == std::string::npos);
    REQUIRE(bot.find("g_Vars.props + aibot->attackpropnum") ==
            std::string::npos);
    REQUIRE(bot.find("g_Vars.props + aibot->followprotectpropnum") ==
            std::string::npos);
}
