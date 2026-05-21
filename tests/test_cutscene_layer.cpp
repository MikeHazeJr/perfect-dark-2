/*
 * test_cutscene_layer.cpp -- Cohort 4 invariants for the Mission 1
 * obj 2 cutscene flash fix.
 *
 * Asserts the two layers of defense:
 *   - belt: LAYER_CUTSCENE's on_push hook clears every gameplay-only
 *           action state, then clears the cutscene action set including
 *           ACTION_USE / ACTION_MENU_ACCEPT before the cutscene tick begins.
 *   - braces: cutscene skip is a hold threshold -- even if a stale
 *             state reaches the tick path, no skip fires without a deliberate
 *             held skip action during the cutscene.
 *
 * Also asserts that scene CUTSCENE_START / CUTSCENE_END round-trip
 * leaves the layer stack in a clean state suitable for the next
 * cutscene to push.
 *
 * Bug repro: see context/designs/input-universality-and-transitions-2026-04-27.md
 * Section H.1 for the full step-by-step.
 *
 * @SYNC port/src/inputlayer.c (onCutscenePush hook)
 * @SYNC port/src/actionmap.cpp (actionmapFlushGameplayState,
 *       actionmapFlushActionSet)
 * @SYNC src/game/player.c (cutscene hold-to-skip gate)
 * @SYNC src/game/player.c:2835 (sceneFire CUTSCENE_START hook)
 *
 * Logging channel reserved for runtime diagnostics: CUTSCENE.LAYER.*
 */

#include "catch.hpp"

#include <fstream>
#include <sstream>
#include <string>

extern "C" {
#include "actionmap_pure.h"
#include "inputlayer_pure.h"
#include "scene_pure.h"
}

namespace {

void resetAll()
{
    spShutdown();
    ilpShutdown();
    ampReset();
    ilpInstrumentReset(nullptr);
    spInstrumentReset(nullptr);
    ilpInit();
    spInit();
}

/* Simulate the onCutscenePush hook firing during a cutscene
 * layer push. In production this is done automatically by
 * inputlayer.c's g_LayerCutscene.on_push field; the pure-C mirrors
 * do not run real callbacks tied to the actionmap module, so the test
 * invokes the flushes directly to mirror the contract. */
void simulateCutsceneStartWithFlushHook()
{
    spFire(SP_SCENE_EVENT_CUTSCENE_START, nullptr);
    ampFlushGameplayState();
    int count = 0;
    const AmpInputAction *set = ampCutsceneActionSet(&count);
    ampFlushActionSet(set, count);
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

TEST_CASE("cutscene flash fix: belt -- on_push hook clears gameplay-only state", "[cutscene][bug][flash][belt]")
{
    resetAll();
    spFire(SP_SCENE_EVENT_STAGE_READY, nullptr);

    /* Player holds a mix of gameplay-only and shared actions, mirroring
     * what happens after a menu-accept event bleeds through. */
    ampSetHeld(0, AMP_ACTION_FIRE_PRIMARY);   /* gameplay-only: must clear */
    ampSetHeld(0, AMP_ACTION_RELOAD);         /* gameplay-only: must clear */
    ampSetHeld(0, AMP_ACTION_WEAPON_NEXT);    /* gameplay-only: must clear */
    ampSetHeld(0, AMP_ACTION_USE);            /* shared accept: must clear */
    ampSetHeld(0, AMP_ACTION_MENU_DOWN);      /* unrelated menu state persists */

    /* Cutscene starts -- on_push hook fires the flush. */
    simulateCutsceneStartWithFlushHook();
    REQUIRE(ilpTopType() == ILP_LAYER_CUTSCENE);

    /* Belt: gameplay-only actions cleared. */
    REQUIRE(ampGetState(0, AMP_ACTION_FIRE_PRIMARY)->held == 0);
    REQUIRE(ampGetState(0, AMP_ACTION_RELOAD)->held       == 0);
    REQUIRE(ampGetState(0, AMP_ACTION_WEAPON_NEXT)->held  == 0);
    REQUIRE(ampGetState(0, AMP_ACTION_USE)->held       == 0);
    REQUIRE(ampGetState(0, AMP_ACTION_USE)->released   == 1);
    REQUIRE(ampGetState(0, AMP_ACTION_MENU_DOWN)->held == 1);
}

TEST_CASE("cutscene flash fix: braces -- stale held state is cleared before hold-to-skip", "[cutscene][bug][flash][braces]")
{
    /* Stale input held since before the cutscene started has no usable
     * skip edge or hold state after the cutscene layer flush. */
    resetAll();

    /* Press USE on the menu accept frame. */
    ampSetPressed(0, AMP_ACTION_USE);
    REQUIRE(ampGetState(0, AMP_ACTION_USE)->pressed == 1);
    REQUIRE(ampGetState(0, AMP_ACTION_USE)->held    == 1);

    /* End-of-frame edge clearing (mirrors actionmapEndFrame) -- the
     * production runtime does this; we simulate by zeroing pressed. */
    AmpActionState *st = (AmpActionState *)ampGetState(0, AMP_ACTION_USE);
    st->pressed = 0;
    /* held stays 1 because the user has not released. */

    REQUIRE(st->held    == 1);
    REQUIRE(st->pressed == 0);

    /* Stage swap, cutscene starts. The transition flush clears USE
     * entirely before hold-to-skip can observe it. */
    simulateCutsceneStartWithFlushHook();
    REQUIRE(st->held    == 0);
    REQUIRE(st->pressed == 0);

    /* Throughout the cutscene, no stale press remains. */
    for (int frame = 0; frame < 100; frame++) {
        REQUIRE(st->pressed == 0);
    }
}

TEST_CASE("cutscene flash fix: bug invariant -- the Mission 1 obj 2 repro", "[cutscene][bug][flash][repro]")
{
    /* Direct mirror of the repro Mike reported:
     *   1. End screen open. Player holds USE to click "Continue".
     *   2. menuhandlerAcceptMission queues mainChangeToStage.
     *   3. Stage swap. AI script runs aiSetCameraAnimation.
     *   4. playerStartCutscene -> playerStartCutscene2 fires
     *      sceneFire(CUTSCENE_START), pushing LAYER_CUTSCENE; the
     *      on_push hook clears gameplay-only and cutscene action-set state.
     *   5. playerTickCutscene polls hold-to-skip. ACTION_USE is no
     *      longer held and pressed = 0.
     *   6. The 30-frame gate is moot because the held state is gone.
     *   7. Cutscene plays its full duration. No flash. */
    resetAll();

    /* Step 1: USE pressed on endscreen Continue. Capture the press
     * frame, then simulate end-of-frame edge clearing. */
    spFire(SP_SCENE_EVENT_STAGE_READY, nullptr); /* gameplay layer up */
    ampSetPressed(0, AMP_ACTION_USE);
    AmpActionState *useSt = (AmpActionState *)ampGetState(0, AMP_ACTION_USE);
    useSt->pressed = 0; /* edge cleared at end of frame */
    REQUIRE(useSt->held == 1); /* user is still holding */

    /* Step 2-4: stage swap and cutscene start. */
    simulateCutsceneStartWithFlushHook();
    REQUIRE(ilpTopType() == ILP_LAYER_CUTSCENE);

    /* Step 5-6: the would-be skip checks all read 0. */
    REQUIRE(ampGetState(0, AMP_ACTION_USE)->held               == 0);
    REQUIRE(ampGetState(0, AMP_ACTION_USE)->pressed            == 0);
    REQUIRE(ampGetState(0, AMP_ACTION_FIRE_PRIMARY)->pressed   == 0);
    REQUIRE(ampGetState(0, AMP_ACTION_RELOAD)->pressed         == 0);
    REQUIRE(ampGetState(0, AMP_ACTION_WEAPON_NEXT)->pressed    == 0);
    REQUIRE(ampGetState(0, AMP_ACTION_PAUSE)->pressed          == 0);

    /* Step 7: cutscene plays. END pops cleanly. */
    spFire(SP_SCENE_EVENT_CUTSCENE_END, nullptr);
    REQUIRE(ilpTopType() == ILP_LAYER_GAMEPLAY);
}

TEST_CASE("cutscene: fresh skip press arms hold state for skip", "[cutscene][skip][positive]")
{
    /* A player who wants to skip the cutscene presses a fresh keydown
     * during it. That arms the held state; production skip now waits for
     * the hold threshold rather than firing on this edge alone. */
    resetAll();
    spFire(SP_SCENE_EVENT_STAGE_READY, nullptr);
    simulateCutsceneStartWithFlushHook();

    /* Mid-cutscene, player presses Space (ACTION_SKIP_CUTSCENE). */
    ampSetPressed(0, AMP_ACTION_SKIP_CUTSCENE);

    REQUIRE(ampGetState(0, AMP_ACTION_SKIP_CUTSCENE)->pressed == 1);
    REQUIRE(ampGetState(0, AMP_ACTION_SKIP_CUTSCENE)->held == 1);
}

TEST_CASE("cutscene: round-trip cleans up handles for the next cutscene", "[cutscene][lifecycle]")
{
    resetAll();
    spFire(SP_SCENE_EVENT_STAGE_READY, nullptr);

    /* First cutscene */
    simulateCutsceneStartWithFlushHook();
    REQUIRE(ilpTopType() == ILP_LAYER_CUTSCENE);
    spFire(SP_SCENE_EVENT_CUTSCENE_END, nullptr);
    REQUIRE(ilpTopType() == ILP_LAYER_GAMEPLAY);

    /* Second cutscene -- handle cache must have been cleared. */
    simulateCutsceneStartWithFlushHook();
    REQUIRE(ilpTopType() == ILP_LAYER_CUTSCENE);
}

TEST_CASE("cutscene: STAGE_TEARDOWN during cutscene unwinds cleanly", "[cutscene][teardown]")
{
    /* If a stage transition fires while a cutscene is active (e.g.,
     * disconnect or main-menu return), STAGE_TEARDOWN must abort the
     * cutscene layer cleanly so the next gameplay/cutscene cycle
     * starts from a known good state. */
    resetAll();
    spFire(SP_SCENE_EVENT_STAGE_READY,    nullptr);
    simulateCutsceneStartWithFlushHook();
    REQUIRE(ilpTopType() == ILP_LAYER_CUTSCENE);

    spFire(SP_SCENE_EVENT_STAGE_TEARDOWN, nullptr);
    REQUIRE(ilpTopType() == ILP_LAYER_BOOT);

    /* Subsequent gameplay + cutscene cycle works. */
    spFire(SP_SCENE_EVENT_STAGE_READY,    nullptr);
    simulateCutsceneStartWithFlushHook();
    REQUIRE(ilpTopType() == ILP_LAYER_CUTSCENE);
}

TEST_CASE("cutscene: skip endstage cleanup permits next mission intro fresh push", "[cutscene][bug][B-267]")
{
    resetAll();
    spInstrumentReset(nullptr);

    spFire(SP_SCENE_EVENT_STAGE_READY, nullptr);
    simulateCutsceneStartWithFlushHook();
    REQUIRE(ilpTopType() == ILP_LAYER_CUTSCENE);

    ampSetPressed(0, AMP_ACTION_SKIP_CUTSCENE);
    REQUIRE(ampGetState(0, AMP_ACTION_SKIP_CUTSCENE)->pressed == 1);

    spFire(SP_SCENE_EVENT_CUTSCENE_END, nullptr);
    REQUIRE(ilpTopType() == ILP_LAYER_GAMEPLAY);
    REQUIRE(ilpHas(ILP_LAYER_CUTSCENE) == 0);

    ampSetPressed(0, AMP_ACTION_USE);
    AmpActionState *useSt = (AmpActionState *)ampGetState(0, AMP_ACTION_USE);
    useSt->pressed = 0;
    REQUIRE(useSt->held == 1);

    spFire(SP_SCENE_EVENT_STAGE_TEARDOWN, nullptr);
    REQUIRE(ilpTopType() == ILP_LAYER_BOOT);

    spFire(SP_SCENE_EVENT_STAGE_READY, nullptr);
    simulateCutsceneStartWithFlushHook();
    REQUIRE(ilpTopType() == ILP_LAYER_CUTSCENE);
    REQUIRE(ampGetState(0, AMP_ACTION_USE)->held == 0);
    REQUIRE(ampGetState(0, AMP_ACTION_USE)->pressed == 0);

    const SpFireCounts *c = spInstrumentGet();
    REQUIRE(c->fire_count[SP_SCENE_EVENT_CUTSCENE_START] == 2);
    REQUIRE(c->fire_count[SP_SCENE_EVENT_CUTSCENE_END] == 1);
}

TEST_CASE("cutscene lifecycle wiring: central paths all fire scene events", "[cutscene][static][B-267]")
{
    const std::string pdmain = readTextFile("port/src/pdmain.c");
    const std::string player = readTextFile("src/game/player.c");
    const std::string netmsg = readTextFile("port/src/net/netmsg.c");
    const std::string net = readTextFile("port/src/net/net.c");
    const std::string cmake = readTextFile("CMakeLists.txt");

    REQUIRE_FALSE(pdmain.empty());
    REQUIRE_FALSE(player.empty());
    REQUIRE_FALSE(netmsg.empty());
    REQUIRE_FALSE(net.empty());
    REQUIRE_FALSE(cmake.empty());

    const std::string setTickMode = functionBlock(player, "playerSetTickMode");
    REQUIRE_FALSE(setTickMode.empty());
    REQUIRE(setTickMode.find("prevtickmode == TICKMODE_CUTSCENE") != std::string::npos);
    REQUIRE(setTickMode.find("sceneFire(SCENE_EVENT_CUTSCENE_END, NULL)") != std::string::npos);

    const std::string endStage = functionBlock(pdmain, "mainEndStage");
    REQUIRE_FALSE(endStage.empty());
    REQUIRE(endStage.find("sceneFire(SCENE_EVENT_CUTSCENE_END, NULL)") != std::string::npos);

    REQUIRE(pdmain.find("sceneFire(SCENE_EVENT_STAGE_READY, NULL)") != std::string::npos);
    REQUIRE(pdmain.find("sceneFire(SCENE_EVENT_STAGE_TEARDOWN, NULL)") != std::string::npos);

    const std::string cutsceneRead = functionBlock(netmsg, "netmsgSvcCutsceneRead");
    REQUIRE_FALSE(cutsceneRead.empty());
    REQUIRE(cutsceneRead.find("SCENE_EVENT_CUTSCENE_START") != std::string::npos);
    REQUIRE(cutsceneRead.find("SCENE_EVENT_CUTSCENE_END") != std::string::npos);

    const std::string disconnect = functionBlock(net, "netDisconnect");
    REQUIRE_FALSE(disconnect.empty());
    /* The literal sceneFire(SCENE_EVENT_DISCONNECT, NULL) call was
     * centralized into sceneStageTransitionPrepare (port/src/scene_transition.c)
     * and is pinned there by test_scene_dispatch.cpp. Here we verify
     * netDisconnect routes through that helper with the disconnect flag. */
    REQUIRE(disconnect.find("SCENE_STAGE_TRANSITION_DISCONNECT") != std::string::npos);
    REQUIRE(disconnect.find("sceneStageTransitionPrepare(") != std::string::npos);

    REQUIRE(cmake.find("src/lib/main.c") == std::string::npos);
    REQUIRE(cmake.find("port/src/pdmain.c") == std::string::npos);
}

TEST_CASE("cutscene skip uses hold prompt and suppresses interact prompts", "[cutscene][skip][prompt][static]")
{
    const std::string actionmapHeader = readTextFile("port/include/actionmap.h");
    const std::string player = readTextFile("src/game/player.c");
    const std::string backend = readTextFile("port/fast3d/pdgui_backend.cpp");
    const std::string prompt = readTextFile("port/fast3d/pdgui_cutscene_prompt.cpp");
    const std::string bridge = readTextFile("port/fast3d/pdgui_bridge.c");
    const std::string glyphs = readTextFile("port/fast3d/pdgui_glyphs.cpp");

    REQUIRE_FALSE(actionmapHeader.empty());
    REQUIRE_FALSE(player.empty());
    REQUIRE_FALSE(backend.empty());
    REQUIRE_FALSE(prompt.empty());
    REQUIRE_FALSE(bridge.empty());
    REQUIRE_FALSE(glyphs.empty());

    const std::string tick = functionBlock(player, "playerTickCutscene");
    const std::string interactSuppress = functionBlock(bridge, "pdguiCiIntroBlocksInteractPrompt");

    REQUIRE_FALSE(tick.empty());
    REQUIRE_FALSE(interactSuppress.empty());

    REQUIRE(actionmapHeader.find("ACTION_SKIP_CUTSCENE_HOLD_THRESHOLD_MS") != std::string::npos);
    REQUIRE(tick.find("ACTION_SKIP_CUTSCENE") != std::string::npos);
    REQUIRE(tick.find("actionHeldForMs(playeridx, skipactions[i], ACTION_SKIP_CUTSCENE_HOLD_THRESHOLD_MS)") != std::string::npos);
    REQUIRE(tick.find("actionConsumeHold(playeridx, skipaction)") != std::string::npos);
    REQUIRE(tick.find("actionPressed(playeridx, ACTION_SKIP_CUTSCENE)") == std::string::npos);

    REQUIRE(prompt.find("pdguiDrawActionPromptCenteredWithHold(action") != std::string::npos);
    REQUIRE(prompt.find("\"Skip\"") != std::string::npos);
    REQUIRE(prompt.find("actionHoldProgress(player, action, ACTION_SKIP_CUTSCENE_HOLD_THRESHOLD_MS)") != std::string::npos);
    REQUIRE(backend.find("pdguiCutsceneSkipPromptShouldRender()") != std::string::npos);
    REQUIRE(backend.find("pdguiCutsceneSkipPromptRender((s32)winW, (s32)winH)") != std::string::npos);
    REQUIRE(glyphs.find("&g_ImcCutscene") != std::string::npos);

    REQUIRE(interactSuppress.find("g_Vars.tickmode == TICKMODE_CUTSCENE") != std::string::npos);
    REQUIRE(interactSuppress.find("playerAnyInCutscene()") != std::string::npos);
}

TEST_CASE("cutscene state migration: migrated gameplay paths use player accessors", "[cutscene][static][state]")
{
    const char *files[] = {
        "src/game/chraction.c",
        "src/game/chraicommands.c",
        "src/game/chr.c",
        "src/game/lv.c",
        "src/game/hudmsg.c",
        "src/lib/vi.c",
        "src/lib/model.c",
        "src/game/prop.c",
        "src/game/propobj.c",
        "src/game/mplayer/mplayer.c",
        "src/game/menu.c",
        "src/game/sky.c",
        "src/game/bondgun.c",
        "src/game/nbomb.c",
    };
    const char *banned[] = {
        "g_Vars.in_cutscene",
        "g_CutsceneSkipRequested",
        "g_CutsceneAnimNum",
        "g_CutsceneCurAnimFrame60",
        "g_CutsceneCurTotalFrame60f",
    };

    for (const char *path : files) {
        const std::string text = readTextFile(path);
        INFO(path);
        REQUIRE_FALSE(text.empty());

        for (const char *token : banned) {
            INFO(token);
            REQUIRE(text.find(token) == std::string::npos);
        }
    }
}

TEST_CASE("cutscene active migration: legacy active globals are retired from production paths", "[cutscene][static][state]")
{
    const char *files[] = {
        "src/include/data.h",
        "src/include/constants.h",
        "src/include/game/player.h",
        "src/game/lv.c",
        "src/game/hudmsg.c",
        "src/lib/vi.c",
        "src/game/player.c",
        "src/game/propobj.c",
        "src/game/mplayer/mplayer.c",
        "src/game/menu.c",
        "src/game/sky.c",
        "port/src/net/netmsg.c",
        "port/src/server_stubs.c",
    };

    for (const char *path : files) {
        const std::string text = readTextFile(path);
        INFO(path);
        REQUIRE_FALSE(text.empty());

        REQUIRE(text.find("g_InCutscene") == std::string::npos);
    }
}

TEST_CASE("cutscene active migration: legacy frame globals are retired", "[cutscene][static][state]")
{
    const char *files[] = {
        "src/include/bss.h",
        "src/include/game/player.h",
        "src/game/player.c",
        "src/game/playermgr.c",
    };
    const char *retired[] = {
        "g_CutsceneSkipRequested",
        "g_CutsceneAnimNum",
        "g_CutsceneCurAnimFrame60",
        "g_CutsceneCurTotalFrame60f",
        "playerSyncCutsceneGlobalsToCurrent",
    };

    for (const char *path : files) {
        const std::string text = readTextFile(path);
        INFO(path);
        REQUIRE_FALSE(text.empty());

        for (const char *token : retired) {
            INFO(token);
            REQUIRE(text.find(token) == std::string::npos);
        }
    }
}

TEST_CASE("cutscene protect: canonical gates use chr flag", "[cutscene][static][protect]")
{
    const std::string types = readTextFile("src/include/types.h");
    const std::string player = readTextFile("src/game/player.c");
    const std::string chraction = readTextFile("src/game/chraction.c");
    const std::string bot = readTextFile("src/game/bot.c");

    REQUIRE_FALSE(types.empty());
    REQUIRE_FALSE(player.empty());
    REQUIRE_FALSE(chraction.empty());
    REQUIRE_FALSE(bot.empty());

    REQUIRE(types.find("cutscene_protect") != std::string::npos);

    REQUIRE(player.find("static void playerRefreshCutsceneProtect(void)") != std::string::npos);
    REQUIRE(player.find("player->prop->chr->cutscene_protect = playerInCutscene(i)") != std::string::npos);

    REQUIRE(chraction.find("if (chr->cutscene_protect)") != std::string::npos);
    REQUIRE(chraction.find("CUTSCENE.DAMAGE.IGNORED") != std::string::npos);

    REQUIRE(chraction.find("checktype == COMPARE_ENEMIES && chr2->cutscene_protect") != std::string::npos);

    const std::string los = functionBlock(chraction, "chrHasLosToChr");
    REQUIRE_FALSE(los.empty());
    REQUIRE(los.find("target->cutscene_protect") != std::string::npos);

    const std::string invisible = functionBlock(bot, "botIsTargetInvisible");
    REQUIRE_FALSE(invisible.empty());
    REQUIRE(invisible.find("otherchr->cutscene_protect") != std::string::npos);
}

TEST_CASE("cutscene network semantics: v46 carries mask and client skip authority", "[cutscene][static][net][v46]")
{
    const std::string netmsgHeader = readTextFile("port/include/net/netmsg.h");
    const std::string netHeader = readTextFile("port/include/net/net.h");
    const std::string netmsg = readTextFile("port/src/net/netmsg.c");
    const std::string net = readTextFile("port/src/net/net.c");
    const std::string player = readTextFile("src/game/player.c");
    const std::string chrai = readTextFile("src/game/chraicommands.c");

    REQUIRE_FALSE(netmsgHeader.empty());
    REQUIRE_FALSE(netHeader.empty());
    REQUIRE_FALSE(netmsg.empty());
    REQUIRE_FALSE(net.empty());
    REQUIRE_FALSE(player.empty());
    REQUIRE_FALSE(chrai.empty());

    REQUIRE(netHeader.find("SVC_CUTSCENE") != std::string::npos);
    REQUIRE(netHeader.find("player_mask") != std::string::npos);
    REQUIRE(netHeader.find("CLC_CUTSCENE_SKIP") != std::string::npos);

    REQUIRE(netmsgHeader.find("#define CLC_CUTSCENE_SKIP         0x17") != std::string::npos);
    REQUIRE(netmsgHeader.find("netmsgSvcCutsceneWrite(struct netbuf *dst, u8 active, u8 player_mask)") != std::string::npos);
    REQUIRE(netmsgHeader.find("netmsgClcCutsceneSkipWrite(struct netbuf *dst, u8 playernum)") != std::string::npos);
    REQUIRE(netmsgHeader.find("netmsgClcCutsceneSkipRead(struct netbuf *src, struct netclient *srccl)") != std::string::npos);

    const std::string svcWrite = functionBlock(netmsg, "netmsgSvcCutsceneWrite");
    REQUIRE_FALSE(svcWrite.empty());
    REQUIRE(svcWrite.find("netbufWriteU8(dst, SVC_CUTSCENE)") != std::string::npos);
    REQUIRE(svcWrite.find("netbufWriteU8(dst, active)") != std::string::npos);
    REQUIRE(svcWrite.find("netbufWriteU8(dst, player_mask)") != std::string::npos);

    const std::string svcRead = functionBlock(netmsg, "netmsgSvcCutsceneRead");
    REQUIRE_FALSE(svcRead.empty());
    REQUIRE(svcRead.find("u8 player_mask = netbufReadU8(src)") != std::string::npos);
    REQUIRE(svcRead.find("playerSetCutsceneActiveMask(player_mask, active ? true : false)") != std::string::npos);
    REQUIRE(svcRead.find("SCENE_EVENT_CUTSCENE_START") != std::string::npos);
    REQUIRE(svcRead.find("SCENE_EVENT_CUTSCENE_END") != std::string::npos);

    const std::string clcWrite = functionBlock(netmsg, "netmsgClcCutsceneSkipWrite");
    REQUIRE_FALSE(clcWrite.empty());
    REQUIRE(clcWrite.find("netbufWriteU8(dst, CLC_CUTSCENE_SKIP)") != std::string::npos);
    REQUIRE(clcWrite.find("netbufWriteU8(dst, playernum)") != std::string::npos);

    const std::string clcRead = functionBlock(netmsg, "netmsgClcCutsceneSkipRead");
    REQUIRE_FALSE(clcRead.empty());
    REQUIRE(clcRead.find("const u8 requested_playernum = netbufReadU8(src)") != std::string::npos);
    REQUIRE(clcRead.find("playernum = srccl->playernum") != std::string::npos);
    REQUIRE(clcRead.find("playerAnyInCutscene()") != std::string::npos);
    REQUIRE(clcRead.find("playerSetCutsceneSkipRequested(playernum, true)") != std::string::npos);

    REQUIRE(net.find("case CLC_CUTSCENE_SKIP") != std::string::npos);
    REQUIRE(player.find("playerBuildCutsceneNetworkMask") != std::string::npos);
    REQUIRE(player.find("playerSetCutsceneActiveMask(cutscene_mask, true)") != std::string::npos);
    REQUIRE(player.find("playerSetCutsceneActiveMask(cutscene_mask, false)") != std::string::npos);
    REQUIRE(player.find("netmsgClcCutsceneSkipWrite(&g_NetMsgRel") != std::string::npos);
    REQUIRE(chrai.find("playerAnyCutsceneInProgress() && playerAnyCutsceneSkipRequested()") != std::string::npos);
}
