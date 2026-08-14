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

    const std::string setTickMode = functionBlock(player,
        "bool playerSetTickMode");
    REQUIRE_FALSE(setTickMode.empty());
    REQUIRE(setTickMode.find("prevtickmode == TICKMODE_CUTSCENE") != std::string::npos);
    REQUIRE(setTickMode.find("sceneFire(SCENE_EVENT_CUTSCENE_END, NULL)") != std::string::npos);

    const std::string endStage = functionBlock(pdmain, "mainEndStage");
    REQUIRE_FALSE(endStage.empty());
    REQUIRE(endStage.find("sceneFire(SCENE_EVENT_CUTSCENE_END, NULL)") != std::string::npos);

    REQUIRE(pdmain.find("sceneFire(SCENE_EVENT_STAGE_READY, NULL)") != std::string::npos);
    REQUIRE(pdmain.find("sceneFire(SCENE_EVENT_STAGE_TEARDOWN, NULL)") != std::string::npos);

    const std::string cutsceneRead = functionBlock(netmsg, "netmsgSvcCutsceneRead");
    const std::string authoritativeApply = functionBlock(player,
        "bool playerApplyAuthoritativeCutsceneState");
    REQUIRE_FALSE(cutsceneRead.empty());
    REQUIRE_FALSE(authoritativeApply.empty());
    REQUIRE(cutsceneRead.find("playerApplyAuthoritativeCutsceneState") !=
        std::string::npos);
    REQUIRE(authoritativeApply.find("SCENE_EVENT_CUTSCENE_START") !=
        std::string::npos);
    REQUIRE(authoritativeApply.find("SCENE_EVENT_CUTSCENE_END") !=
        std::string::npos);

    const std::string disconnectEntry = functionBlock(net, "netDisconnect");
    const std::string disconnect = functionBlock(net,
        "netDisconnectWithIntent");
    REQUIRE_FALSE(disconnectEntry.empty());
    REQUIRE_FALSE(disconnect.empty());
    REQUIRE(disconnectEntry.find("netDisconnectWithIntent(false)") !=
        std::string::npos);
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

TEST_CASE("cutscene network semantics: v56 owns one ordered match authority stream", "[cutscene][static][net][v56][b1089]")
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

    REQUIRE(netHeader.find("v56 (2026-08-13): cutscene authority") != std::string::npos);
    REQUIRE(netHeader.find("stable client-ID") != std::string::npos);
    REQUIRE(netHeader.find("SVC_CUTSCENE_SKIP") != std::string::npos);

    REQUIRE(netmsgHeader.find("#define CLC_CUTSCENE_SKIP         0x17") != std::string::npos);
    REQUIRE(netmsgHeader.find("#define SVC_CUTSCENE_SKIP 0x54") != std::string::npos);
    REQUIRE(netmsgHeader.find("NET_CUTSCENE_AUTHORITY_EVENT_CAPACITY 64u") != std::string::npos);
    REQUIRE(netmsgHeader.find("NET_CUTSCENE_AUTHORITY_PACKET_CAPACITY 1024u") != std::string::npos);
    REQUIRE(netmsgHeader.find("netmsgServerStageStartWrite") != std::string::npos);
    REQUIRE(netmsgHeader.find("netmsgServerBeginCutsceneAuthority") == std::string::npos);
    REQUIRE(netmsgHeader.find("netmsgServerQueueCutsceneState") != std::string::npos);
    REQUIRE(netmsgHeader.find("netmsgServerPrepareCutsceneAuthorityPacket") != std::string::npos);
    REQUIRE(netmsgHeader.find("netmsgServerCommitCutsceneAuthorityPacket") != std::string::npos);
    REQUIRE(netmsgHeader.find("netmsgCutsceneAuthorityHasMatch") != std::string::npos);
    REQUIRE(netmsgHeader.find("netmsgCutsceneAuthorityHasPendingEvents") !=
        std::string::npos);
    REQUIRE(netmsgHeader.find("netmsgCutsceneAuthorityRetireClient") != std::string::npos);
    REQUIRE(netmsgHeader.find("netmsgSvcCutsceneWrite") == std::string::npos);
    REQUIRE(netmsgHeader.find("netmsgSvcCutsceneSkipWrite") == std::string::npos);
    REQUIRE(netmsgHeader.find("netmsgClcCutsceneSkipWrite(struct netbuf *dst, u8 playernum,") != std::string::npos);
    REQUIRE(netmsgHeader.find("netmsgClcCutsceneSkipRead(struct netbuf *src, struct netclient *srccl)") != std::string::npos);

    const std::string collect = functionBlock(netmsg,
        "netmsgCollectCutsceneParticipants");
    REQUIRE_FALSE(collect.empty());
    REQUIRE(collect.find("s_CutsceneAuthority.participants") !=
        std::string::npos);
    REQUIRE(collect.find("memcpy(participants") != std::string::npos);
    REQUIRE(collect.find("g_NetClients") == std::string::npos);

    const std::string preparedWrite = functionBlock(netmsg,
        "netmsgSvcStageStartWriteInternal");
    const std::string serverWrite = functionBlock(netmsg,
        "netmsgServerStageStartWrite");
    REQUIRE_FALSE(preparedWrite.empty());
    REQUIRE_FALSE(serverWrite.empty());
    REQUIRE(preparedWrite.find("prepared->client_id") != std::string::npos);
    REQUIRE(preparedWrite.find("prepared->playernum") != std::string::npos);
    REQUIRE(preparedWrite.find("prepared->options") != std::string::npos);
    REQUIRE(preparedWrite.find("prepared->name") != std::string::npos);
    REQUIRE(preparedWrite.find("s_StageStartAuthorityCandidate.participants") !=
        std::string::npos);
    REQUIRE(serverWrite.find("netmsgSvcStageStartWriteInternal(dst, true, true)") !=
        std::string::npos);
    REQUIRE(serverWrite.find("netmsgCutsceneAuthorityBeginMatch") !=
        std::string::npos);
    REQUIRE(serverWrite.find("s_StageStartAuthorityCandidate.participants") !=
        std::string::npos);
    REQUIRE(serverWrite.find("g_NetClients") == std::string::npos);

    const std::string stateQueue = functionBlock(netmsg,
        "netmsgServerQueueCutsceneState");
    REQUIRE_FALSE(stateQueue.empty());
    REQUIRE(stateQueue.find("s_CutsceneAuthority.full_client_mask") !=
        std::string::npos);
    REQUIRE(stateQueue.find("netCutscenePlanStateTransition") !=
        std::string::npos);
    REQUIRE(stateQueue.find("netmsgQueueCutsceneEvent") !=
        std::string::npos);
    REQUIRE(stateQueue.find("s_CutsceneAuthority.tracker = next") !=
        std::string::npos);
    REQUIRE(stateQueue.find("g_NetMsgRel") == std::string::npos);

    const std::string prepare = functionBlock(netmsg,
        "netmsgServerPrepareCutsceneAuthorityPacket");
    const std::string commit = functionBlock(netmsg,
        "netmsgServerCommitCutsceneAuthorityPacket");
    REQUIRE_FALSE(prepare.empty());
    REQUIRE_FALSE(commit.empty());
    REQUIRE(prepare.find("netmsgWriteCutsceneState") != std::string::npos);
    REQUIRE(prepare.find("netmsgSvcCutsceneSkipWrite") != std::string::npos);
    REQUIRE(prepare.find("s_CutsceneAuthority.event_count") !=
        std::string::npos);
    REQUIRE(commit.find("memmove(s_CutsceneAuthority.events") !=
        std::string::npos);
    REQUIRE(commit.find("s_CutsceneAuthority.event_count -= event_count") !=
        std::string::npos);

    const std::string svcRead = functionBlock(netmsg, "netmsgSvcCutsceneRead");
    REQUIRE_FALSE(svcRead.empty());
    REQUIRE(svcRead.find("state.client_mask != s_CutsceneAuthority.full_client_mask") != std::string::npos);
    REQUIRE(svcRead.find("netCutscenePlanStateTransition") != std::string::npos);
    REQUIRE(svcRead.find("netCutscenePlayerMaskFromClientMask") != std::string::npos);
    REQUIRE(svcRead.find("playerApplyAuthoritativeCutsceneState") != std::string::npos);
    REQUIRE(svcRead.find("s_CutsceneAuthority.tracker = next") != std::string::npos);
    REQUIRE(svcRead.find("ignored outside active match") != std::string::npos);

    const std::string clcWrite = functionBlock(netmsg, "netmsgClcCutsceneSkipWrite");
    REQUIRE_FALSE(clcWrite.empty());
    REQUIRE(clcWrite.find("netCutsceneSkipRequestEncode") != std::string::npos);
    REQUIRE(clcWrite.find("netmsgWriteCutscenePayload(dst, CLC_CUTSCENE_SKIP") != std::string::npos);

    const std::string clcRead = functionBlock(netmsg, "netmsgClcCutsceneSkipRead");
    REQUIRE_FALSE(clcRead.empty());
    REQUIRE(clcRead.find("netCutscenePlanServerSkip") != std::string::npos);
    REQUIRE(clcRead.find("netmsgCutscenePlayerForClient(srccl->id") != std::string::npos);
    REQUIRE(clcRead.find("srccl->playernum") == std::string::npos);
    REQUIRE(clcRead.find("netmsgQueueCutsceneSkipAccept") != std::string::npos);
    REQUIRE(clcRead.find("playerSetCutsceneSkipRequested(authoritative_playernum, true)") != std::string::npos);
    REQUIRE(clcRead.find("netmsgQueueCutsceneSkipAccept") <
        clcRead.find("playerSetCutsceneSkipRequested"));

    const std::string svcSkipRead = functionBlock(netmsg, "netmsgSvcCutsceneSkipRead");
    REQUIRE_FALSE(svcSkipRead.empty());
    REQUIRE(svcSkipRead.find("netCutscenePlanClientSkip") != std::string::npos);
    REQUIRE(svcSkipRead.find("playerSetCutsceneSkipRequested(runtime_playernum, true)") != std::string::npos);
    REQUIRE(svcSkipRead.find("ignored outside active match") != std::string::npos);
    const size_t inactiveGate = svcSkipRead.find(
        "NET_CUTSCENE_AUTHORITY_PHASE_ACTIVE");
    const size_t rosterCollect = svcSkipRead.find("netmsgCollectCutsceneParticipants");
    REQUIRE(inactiveGate != std::string::npos);
    REQUIRE(rosterCollect != std::string::npos);
    REQUIRE(inactiveGate < rosterCollect);
    REQUIRE(svcSkipRead.find("g_NetClients") == std::string::npos);

    REQUIRE(net.find("case CLC_CUTSCENE_SKIP") != std::string::npos);
    REQUIRE(net.find("case SVC_CUTSCENE_SKIP") != std::string::npos);
    const std::string flush = functionBlock(net,
        "netServerFlushCutsceneAuthorityPacket");
    REQUIRE_FALSE(flush.empty());
    REQUIRE(flush.find("netmsgServerPrepareCutsceneAuthorityPacket") !=
        std::string::npos);
    REQUIRE(flush.find(
        "netSendToRoom(room_id, &wire, true, NETCHAN_DEFAULT)") !=
        std::string::npos);
    REQUIRE(flush.find("netmsgServerCommitCutsceneAuthorityPacket") !=
        std::string::npos);
    REQUIRE(flush.find("netSendToRoom") <
        flush.find("netmsgServerCommitCutsceneAuthorityPacket"));
    REQUIRE(flush.find("netmsgSvcStageEndWrite") <
        flush.find("netSendToRoom"));
    const std::string serverStart = functionBlock(net,
        "netServerStageStart");
    const std::string coopStart = functionBlock(net,
        "netServerCoopStageStart");
    REQUIRE_FALSE(serverStart.empty());
    REQUIRE_FALSE(coopStart.empty());
    REQUIRE(serverStart.find("netmsgServerStageStartWrite") !=
        std::string::npos);
    REQUIRE(serverStart.find("netmsgServerStageStartWrite") <
        serverStart.find("mpStartMatch()"));
    REQUIRE(coopStart.find("netmsgServerStageStartWrite") !=
        std::string::npos);

    const std::string clientStageStart = functionBlock(netmsg,
        "netmsgSvcStageStartRead");
    REQUIRE_FALSE(clientStageStart.empty());
    REQUIRE(clientStageStart.find("netmsgCutsceneAuthorityBeginMatch") !=
        std::string::npos);
    REQUIRE(clientStageStart.find("netmsgCutsceneAuthorityBeginMatch") <
        clientStageStart.find("g_NetTick = plan.net_tick"));

    const std::string serverEnd = functionBlock(net, "netServerStageEnd");
    const std::string stageEndFlush = functionBlock(net,
        "netServerFlushPendingStageEnd");
    const std::string stageEndCommit = functionBlock(net,
        "netServerCommitPendingStageEnd");
    const std::string stageEndWrite = functionBlock(netmsg,
        "netmsgSvcStageEndWrite");
    const std::string stageEndWireCommit = functionBlock(netmsg,
        "netmsgSvcStageEndCommit");
    REQUIRE_FALSE(serverEnd.empty());
    REQUIRE_FALSE(stageEndFlush.empty());
    REQUIRE_FALSE(stageEndCommit.empty());
    REQUIRE_FALSE(stageEndWrite.empty());
    REQUIRE_FALSE(stageEndWireCommit.empty());
    REQUIRE(serverEnd.find("netmsgServerQueueCutsceneState(0") !=
        std::string::npos);
    REQUIRE(serverEnd.find("netmsgCutsceneAuthorityHasMatch") !=
        std::string::npos);
    const size_t terminalQueue = serverEnd.find(
        "netmsgServerQueueCutsceneState(0");
    const size_t terminalPending = serverEnd.find(
        "s_NetStageEndPending.active = true", terminalQueue);
    const size_t terminalPublish = serverEnd.find(
        "netServerFlushPendingStageEnd(\"stage-end\")", terminalPending);
    REQUIRE(terminalQueue != std::string::npos);
    REQUIRE(terminalPending != std::string::npos);
    REQUIRE(terminalPublish != std::string::npos);
    REQUIRE(terminalQueue < terminalPending);
    REQUIRE(terminalPending < terminalPublish);
    REQUIRE(serverEnd.find("playerResetAllCutsceneStates") ==
        std::string::npos);
    REQUIRE(stageEndFlush.find("netServerFlushCutsceneAuthorityPacket") !=
        std::string::npos);
    REQUIRE(stageEndFlush.find("netServerCommitPendingStageEnd") !=
        std::string::npos);
    REQUIRE(stageEndFlush.find("netServerFlushCutsceneAuthorityPacket") <
        stageEndFlush.find("netServerCommitPendingStageEnd"));
    REQUIRE(stageEndCommit.find("playerResetAllCutsceneStates") !=
        std::string::npos);
    REQUIRE(stageEndCommit.find("netmsgCutsceneAuthorityReset") !=
        std::string::npos);
    REQUIRE(stageEndCommit.find("netmsgSvcStageEndCommit") !=
        std::string::npos);
    REQUIRE(stageEndWrite.find("netbufWriteU8(dst, SVC_STAGE_END)") !=
        std::string::npos);
    REQUIRE(stageEndWrite.find("g_NetClients") == std::string::npos);
    REQUIRE(stageEndWireCommit.find("g_NetClients") != std::string::npos);

    const std::string endFrame = functionBlock(net, "netEndFrame");
    REQUIRE_FALSE(endFrame.empty());
    REQUIRE(endFrame.find("const bool terminal_stage_end_frame") !=
        std::string::npos);
    REQUIRE(endFrame.find("const bool authority_pending_frame") !=
        std::string::npos);
    REQUIRE(endFrame.find("s_NetStageEndPending.active || s_NetStageEndTerminalFrame") !=
        std::string::npos);
    REQUIRE(endFrame.find("netmsgCutsceneAuthorityHasPendingEvents") !=
        std::string::npos);
    const size_t terminalDiscard = endFrame.find(
        "if (terminal_stage_end_frame || authority_pending_frame)");
    const size_t firstSharedFlush = endFrame.find("netFlushSendBuffers()");
    REQUIRE(terminalDiscard != std::string::npos);
    REQUIRE(firstSharedFlush != std::string::npos);
    REQUIRE(terminalDiscard < firstSharedFlush);
    REQUIRE(endFrame.find("netbufStartWrite(&g_NetMsg)", terminalDiscard) <
        firstSharedFlush);
    REQUIRE(endFrame.find("netbufStartWrite(&g_NetMsgRel)", terminalDiscard) <
        firstSharedFlush);
    const size_t priorityPublish = endFrame.find(
        "netServerFlushCutsceneAuthority(\"end-frame-priority\")");
    const size_t spectatorPublish = endFrame.find("netSendSpectateStateFrame");
    const size_t gamePublish = endFrame.find(
        "g_NetMode == NETMODE_SERVER && g_NetNumClients > 0");
    REQUIRE(priorityPublish != std::string::npos);
    REQUIRE(spectatorPublish != std::string::npos);
    REQUIRE(gamePublish != std::string::npos);
    REQUIRE(priorityPublish < spectatorPublish);
    REQUIRE(priorityPublish < gamePublish);
    REQUIRE(endFrame.find("&& !authority_publication_failed", priorityPublish) !=
        std::string::npos);
    REQUIRE(endFrame.find(
        "if (terminal_stage_end_frame || authority_publication_failed)") !=
        std::string::npos);
    REQUIRE(endFrame.find("netSendSpectateStateFrame") >
        endFrame.find("!terminal_stage_end_frame"));
    REQUIRE(endFrame.find("g_NetNumClients > 0\n\t\t\t&& !terminal_stage_end_frame") !=
        std::string::npos);
    REQUIRE(endFrame.find("g_NetMode == NETMODE_SERVER && !terminal_stage_end_frame") !=
        std::string::npos);
    REQUIRE(endFrame.find("s_NetStageEndTerminalFrame = false") !=
        std::string::npos);

    const std::string clientEnd = functionBlock(netmsg,
        "netmsgSvcStageEndRead");
    REQUIRE_FALSE(clientEnd.empty());
    REQUIRE(clientEnd.find("!netmsgCutsceneAuthorityHasMatch()") <
        clientEnd.find("playerResetAllCutsceneStates"));
    REQUIRE(clientEnd.find("s_CutsceneAuthority.match_active") ==
        std::string::npos);
    REQUIRE(clientEnd.find("duplicate_or_stale") != std::string::npos);
    REQUIRE(clientEnd.find("playerResetAllCutsceneStates") <
        clientEnd.find("mainEndStage()"));
    REQUIRE(clientEnd.find("netmsgCutsceneAuthorityReset") <
        clientEnd.find("mainEndStage()"));

    const std::string generationSync = functionBlock(player,
        "bool playerSyncCutsceneGeneration");
    REQUIRE_FALSE(generationSync.empty());
    REQUIRE(generationSync.find("g_NetMode != NETMODE_CLIENT") !=
        std::string::npos);
    REQUIRE(generationSync.find("netCutsceneAuthoritySyncGeneration") !=
        std::string::npos);

    const std::string apply = functionBlock(player,
        "bool playerApplyAuthoritativeCutsceneState");
    REQUIRE_FALSE(apply.empty());
    REQUIRE(apply.find("g_NetMode != NETMODE_CLIENT") != std::string::npos);
    REQUIRE(apply.find("playerSyncCutsceneGeneration") != std::string::npos);
    REQUIRE(apply.find("const bool predicted_start") != std::string::npos);
    REQUIRE(apply.find("if (!predicted_start)") != std::string::npos);
    REQUIRE(apply.find("START reconciled predicted=") != std::string::npos);
    REQUIRE(apply.find("playerReplaceCutsceneActiveMask(player_mask)") !=
        std::string::npos);
    REQUIRE(apply.find("playerReplaceCutsceneActiveMask(0)") !=
        std::string::npos);
    REQUIRE(apply.find("s_CutsceneGeneration = 0") != std::string::npos);

    const std::string replaceMask = functionBlock(player,
        "void playerReplaceCutsceneActiveMask");
    REQUIRE_FALSE(replaceMask.empty());
    REQUIRE(replaceMask.find("i < MAX_PLAYERS && i < 8") !=
        std::string::npos);
    REQUIRE(replaceMask.find("state->active =") != std::string::npos);
    REQUIRE(replaceMask.find("state->in_progress = false") !=
        std::string::npos);
    REQUIRE(replaceMask.find("state->skiprequested = false") !=
        std::string::npos);
    REQUIRE(replaceMask.find("s_CutsceneFallbackState.skiprequested = false") !=
        std::string::npos);

    const std::string tickMode = functionBlock(player,
        "bool playerSetTickMode");
    REQUIRE_FALSE(tickMode.empty());
    REQUIRE(tickMode.find("s_ApplyingAuthoritativeCutsceneState") !=
        std::string::npos);
    const size_t clientExitGuard = tickMode.find(
        "if (g_NetMode == NETMODE_CLIENT");
    const size_t clientExitReturn = tickMode.find("return false;",
        clientExitGuard);
    const size_t serverEndGuard = tickMode.find(
        "if (g_NetMode == NETMODE_SERVER", clientExitReturn);
    REQUIRE(clientExitGuard != std::string::npos);
    REQUIRE(clientExitReturn != std::string::npos);
    REQUIRE(serverEndGuard != std::string::npos);
    REQUIRE(clientExitGuard < clientExitReturn);
    REQUIRE(clientExitReturn < serverEndGuard);
    const std::string clientExitPredicate = tickMode.substr(clientExitGuard,
        clientExitReturn - clientExitGuard);
    REQUIRE(clientExitPredicate.find("netmsgCutsceneAuthorityHasMatch()") !=
        std::string::npos);
    REQUIRE(clientExitPredicate.find("netmsgCutsceneAuthorityIsActive()") ==
        std::string::npos);
    const size_t tickModeEndQueue = tickMode.find(
        "netmsgServerQueueCutsceneState(0");
    const size_t tickModeMutation = tickMode.find(
        "g_Vars.tickmode = tickmode");
    REQUIRE(tickMode.find("const bool leaving_cutscene") !=
        std::string::npos);
    REQUIRE(tickModeEndQueue != std::string::npos);
    REQUIRE(tickModeMutation != std::string::npos);
    REQUIRE(tickModeEndQueue < tickModeMutation);
    REQUIRE(tickMode.find("tick-mode END preflight rejected") !=
        std::string::npos);
    REQUIRE(tickMode.find("return false") < tickModeMutation);
    REQUIRE(tickMode.find("playerReplaceCutsceneActiveMask(0)") !=
        std::string::npos);
    REQUIRE(tickMode.find("playerReplaceCutsceneActiveMask(0)") >
        tickModeMutation);
    REQUIRE(tickMode.find("return true") != std::string::npos);

    const std::string cutsceneStart = functionBlock(player,
        "bool playerStartCutscene2");
    REQUIRE_FALSE(cutsceneStart.empty());
    REQUIRE(cutsceneStart.find(
        "const bool authority_transition = !playerAnyInCutscene();") !=
        std::string::npos);
    REQUIRE(cutsceneStart.find(
        "g_NetMode == NETMODE_CLIENT && authority_transition") !=
        std::string::npos);
    REQUIRE(cutsceneStart.find("} else if (authority_transition)") !=
        std::string::npos);
    REQUIRE(cutsceneStart.find("s_CutsceneGeneration = 0") !=
        std::string::npos);
    REQUIRE(cutsceneStart.find("generation remains zero until reliable") !=
        std::string::npos);
    REQUIRE(cutsceneStart.find("netCutsceneAuthorityNextGeneration") !=
        std::string::npos);
    REQUIRE(cutsceneStart.find("netmsgServerQueueCutsceneState(1") !=
        std::string::npos);
    REQUIRE(cutsceneStart.find("netmsgServerQueueCutsceneState(1") <
        cutsceneStart.find("s_CutsceneGeneration = next_generation"));
    REQUIRE(cutsceneStart.find("netmsgServerQueueCutsceneState(1") <
        cutsceneStart.find("sceneFire(SCENE_EVENT_CUTSCENE_START"));
    REQUIRE(cutsceneStart.find(
        "playerReplaceCutsceneActiveMask(authority_player_mask)") !=
        std::string::npos);
    const size_t authorityStartQueue = cutsceneStart.find(
        "netmsgServerQueueCutsceneState(1");
    const size_t animationCommit = cutsceneStart.find(
        "playerSetCutsceneAnimNum(g_Vars.currentplayernum, animnum)");
    REQUIRE(authorityStartQueue != std::string::npos);
    REQUIRE(animationCommit != std::string::npos);
    REQUIRE(authorityStartQueue < animationCommit);
    REQUIRE(cutsceneStart.find("return false", authorityStartQueue) <
        animationCommit);
    REQUIRE(cutsceneStart.find("return true") != std::string::npos);
    REQUIRE(cutsceneStart.find("g_NetMsgRel") == std::string::npos);
    REQUIRE(cutsceneStart.find("NETGAMEMODE_COOP") == std::string::npos);
    REQUIRE(cutsceneStart.find("NETGAMEMODE_ANTI") == std::string::npos);

    const std::string cutsceneEnd = functionBlock(player,
        "void playerEndCutscene");
    REQUIRE_FALSE(cutsceneEnd.empty());
    REQUIRE(cutsceneEnd.find("g_NetMode == NETMODE_CLIENT") ==
        std::string::npos);
    REQUIRE(cutsceneEnd.find("if (!playerSetTickMode(TICKMODE_NORMAL))") !=
        std::string::npos);
    REQUIRE(cutsceneEnd.find("netmsgServerQueueCutsceneState(0") ==
        std::string::npos);
    REQUIRE(cutsceneEnd.find("SCENE_EVENT_CUTSCENE_END") ==
        std::string::npos);
    REQUIRE(cutsceneEnd.find("g_NetMsgRel") == std::string::npos);
    REQUIRE(cutsceneEnd.find("NETGAMEMODE_COOP") == std::string::npos);
    REQUIRE(cutsceneEnd.find("NETGAMEMODE_ANTI") == std::string::npos);

    const std::string reset = functionBlock(player, "playerResetAllCutsceneStates");
    REQUIRE_FALSE(reset.empty());
    REQUIRE(reset.find("netmsgCutsceneAuthorityReset") == std::string::npos);
    REQUIRE(player.find("playerBuildCutsceneNetworkMask") == std::string::npos);
    REQUIRE(player.find("netmsgClcCutsceneSkipWrite(&g_NetMsgRel") != std::string::npos);
    REQUIRE(player.find("playerCutsceneGeneration()) == 0") != std::string::npos);
    REQUIRE(player.find("&& playerRequestCutsceneSkip(playeridx,") != std::string::npos);
    REQUIRE(chrai.find("playerAnyCutsceneInProgress() && playerAnyCutsceneSkipRequested()") != std::string::npos);
}

TEST_CASE("network clients leave the local MP opening swirl without bypassing cutscene authority",
          "[cutscene][network][static][b1094]")
{
    const std::string player = readTextFile("src/game/player.c");
    REQUIRE_FALSE(player.empty());

    const std::string swirl = functionBlock(player, "void playerTickMpSwirl");
    const std::string end = functionBlock(player, "void playerEndCutscene");
    const std::string transition = functionBlock(player,
        "bool playerSetTickMode");
    REQUIRE_FALSE(swirl.empty());
    REQUIRE_FALSE(end.empty());
    REQUIRE_FALSE(transition.empty());

    REQUIRE(swirl.find("g_MpSwirlDistance < 5.0f") != std::string::npos);
    REQUIRE(swirl.find("playerEndCutscene()") != std::string::npos);
    REQUIRE(swirl.find("g_Vars.tickmode = TICKMODE_NORMAL") ==
        std::string::npos);

    REQUIRE(end.find("if (!playerSetTickMode(TICKMODE_NORMAL))") !=
        std::string::npos);
    REQUIRE(end.find("g_NetMode == NETMODE_CLIENT") == std::string::npos);

    REQUIRE(transition.find(
        "const bool leaving_cutscene = prevtickmode == TICKMODE_CUTSCENE") !=
        std::string::npos);
    REQUIRE(transition.find("g_NetMode == NETMODE_CLIENT") !=
        std::string::npos);
    REQUIRE(transition.find("&& leaving_cutscene") != std::string::npos);
    const size_t clientGuard = transition.find(
        "if (g_NetMode == NETMODE_CLIENT");
    const size_t clientGuardReturn = transition.find("return false;",
        clientGuard);
    REQUIRE(clientGuard != std::string::npos);
    REQUIRE(clientGuardReturn != std::string::npos);
    const std::string clientPredicate = transition.substr(clientGuard,
        clientGuardReturn - clientGuard);
    REQUIRE(clientPredicate.find("netmsgCutsceneAuthorityHasMatch()") !=
        std::string::npos);
    REQUIRE(clientPredicate.find("netmsgCutsceneAuthorityIsActive()") ==
        std::string::npos);
    REQUIRE(transition.find("s_ApplyingAuthoritativeCutsceneState") !=
        std::string::npos);
}

TEST_CASE("validated stage start retires only prior-stage client presentation",
          "[cutscene][network][reconnect][static][b1099]")
{
    const std::string player = readTextFile("src/game/player.c");
    const std::string lv = readTextFile("src/game/lv.c");
    const std::string netmsg = readTextFile("port/src/net/netmsg.c");
    REQUIRE_FALSE(player.empty());
    REQUIRE_FALSE(lv.empty());
    REQUIRE_FALSE(netmsg.empty());

    const std::string boundary = functionBlock(player,
        "bool playerApplyAuthoritativeStageStartPresentation");
    const std::string authoritativeTick = functionBlock(player,
        "static bool playerApplyAuthoritativeTickMode");
    const std::string transition = functionBlock(player,
        "bool playerSetTickMode");
    const std::string reset = functionBlock(lv, "void lvReset");
    const std::string stageStart = functionBlock(netmsg,
        "u32 netmsgSvcStageStartRead");
    REQUIRE_FALSE(boundary.empty());
    REQUIRE_FALSE(authoritativeTick.empty());
    REQUIRE_FALSE(transition.empty());
    REQUIRE_FALSE(reset.empty());
    REQUIRE_FALSE(stageStart.empty());

    REQUIRE(boundary.find("g_NetMode != NETMODE_CLIENT") !=
        std::string::npos);
    REQUIRE(boundary.find("!netmsgCutsceneAuthorityHasMatch()") !=
        std::string::npos);
    REQUIRE(boundary.find("netmsgCutsceneAuthorityIsActive()") !=
        std::string::npos);
    REQUIRE(boundary.find(
        "previous_tickmode == TICKMODE_CUTSCENE") != std::string::npos);
    REQUIRE(boundary.find(
        "playerApplyAuthoritativeTickMode(TICKMODE_GE_FADEIN)") !=
        std::string::npos);
    REQUIRE(boundary.find("playerResetAllCutsceneStates()") !=
        std::string::npos);
    REQUIRE(boundary.find("stage-start presentation previous_tickmode=") !=
        std::string::npos);
    REQUIRE(boundary.find("g_Vars.tickmode =") == std::string::npos);

    REQUIRE(authoritativeTick.find(
        "s_ApplyingAuthoritativeCutsceneState = true") !=
        std::string::npos);
    REQUIRE(authoritativeTick.find("playerSetTickMode(tickmode)") !=
        std::string::npos);
    REQUIRE(authoritativeTick.find(
        "s_ApplyingAuthoritativeCutsceneState = was_applying") !=
        std::string::npos);

    const size_t loadBoundary = reset.find(
        "playerApplyAuthoritativeStageStartPresentation()");
    const size_t fallbackReset = reset.find(
        "playerResetAllCutsceneStates()", loadBoundary);
    const size_t playerReset = reset.find("playerReset();", fallbackReset);
    REQUIRE(loadBoundary != std::string::npos);
    REQUIRE(fallbackReset != std::string::npos);
    REQUIRE(playerReset != std::string::npos);
    REQUIRE(loadBoundary < fallbackReset);
    REQUIRE(fallbackReset < playerReset);

    const size_t finalWireGate = stageStart.find("truncated bot roster");
    const size_t matchLatch = stageStart.find(
        "netmsgCutsceneAuthorityBeginMatch", finalWireGate);
    const size_t publication = stageStart.find(
        "/* One publication point", matchLatch);
    REQUIRE(finalWireGate != std::string::npos);
    REQUIRE(matchLatch != std::string::npos);
    REQUIRE(publication != std::string::npos);
    REQUIRE(finalWireGate < matchLatch);
    REQUIRE(matchLatch < publication);

    const size_t ordinaryClientGuard = transition.find(
        "if (g_NetMode == NETMODE_CLIENT");
    const size_t ordinaryClientReturn = transition.find("return false;",
        ordinaryClientGuard);
    REQUIRE(ordinaryClientGuard != std::string::npos);
    REQUIRE(ordinaryClientReturn != std::string::npos);
    const std::string guard = transition.substr(ordinaryClientGuard,
        ordinaryClientReturn - ordinaryClientGuard);
    REQUIRE(guard.find("netmsgCutsceneAuthorityHasMatch()") !=
        std::string::npos);
    REQUIRE(guard.find("netmsgCutsceneAuthorityIsActive()") ==
        std::string::npos);
}

TEST_CASE("network cutscene presentation restores the receiver-local player before global graph work",
          "[cutscene][network][static][b1090]")
{
    const std::string pdmain = readTextFile("port/src/pdmain.c");
    const std::string playermgr = readTextFile("src/game/playermgr.c");
    const std::string player = readTextFile("src/game/player.c");
    const std::string chrai = readTextFile("src/game/chraicommands.c");
    const std::string scenario = readTextFile("port/src/scenario_source_runtime.c");

    REQUIRE_FALSE(pdmain.empty());
    REQUIRE_FALSE(playermgr.empty());
    REQUIRE_FALSE(player.empty());
    REQUIRE_FALSE(chrai.empty());
    REQUIRE_FALSE(scenario.empty());

    const std::string mainTick = functionBlock(pdmain, "void mainTick");
    const std::string localPlayer = functionBlock(playermgr,
        "s32 playermgrGetLocalPlayerNum");
    const std::string presentationPlayer = functionBlock(playermgr,
        "s32 playermgrGetPresentationPlayerNum");
    const std::string restore = functionBlock(playermgr,
        "bool playermgrRestoreLocalPlayerContext");
    const std::string presentationStart = functionBlock(player,
        "bool playerStartCutsceneForPresentation");
    const std::string presentationCondition = functionBlock(player,
        "bool playerPresentationCutsceneInProgress");
    const std::string fallbackStart = functionBlock(chrai,
        "bool aiSetCameraAnimation");
    const std::string fallbackCondition = functionBlock(chrai,
        "bool aiIfInCutscene");
    const std::string fallbackChrAnimation = functionBlock(chrai,
        "bool aiChrDoAnimation");
    const std::string fallbackObjectAnimation = functionBlock(chrai,
        "bool aiObjectDoAnimation");
    const std::string graphStart = functionBlock(scenario,
        "s32 scenarioSourceAiGraphExecuteSetCameraAnimation");
    const std::string graphCondition = functionBlock(scenario,
        "s32 scenarioSourceAiGraphExecuteIfInCutscene");
    const std::string graphChrAnimation = functionBlock(scenario,
        "s32 scenarioSourceAiGraphExecuteChrDoAnimation");
    const std::string graphObjectAnimation = functionBlock(scenario,
        "s32 scenarioSourceAiGraphExecuteObjectDoAnimation");

    REQUIRE_FALSE(mainTick.empty());
    REQUIRE_FALSE(localPlayer.empty());
    REQUIRE_FALSE(presentationPlayer.empty());
    REQUIRE_FALSE(restore.empty());
    REQUIRE_FALSE(presentationStart.empty());
    REQUIRE_FALSE(presentationCondition.empty());
    REQUIRE_FALSE(fallbackStart.empty());
    REQUIRE_FALSE(fallbackCondition.empty());
    REQUIRE_FALSE(fallbackChrAnimation.empty());
    REQUIRE_FALSE(fallbackObjectAnimation.empty());
    REQUIRE_FALSE(graphStart.empty());
    REQUIRE_FALSE(graphCondition.empty());
    REQUIRE_FALSE(graphChrAnimation.empty());
    REQUIRE_FALSE(graphObjectAnimation.empty());

    const size_t contextRestore = mainTick.find(
        "playermgrRestoreLocalPlayerContext");
    const size_t globalTickGate = mainTick.find("if (run_global_tick)",
        contextRestore);
    const size_t globalTick = mainTick.find("lvTick();", globalTickGate);
    REQUIRE(contextRestore != std::string::npos);
    REQUIRE(globalTickGate != std::string::npos);
    REQUIRE(globalTick != std::string::npos);
    REQUIRE(contextRestore < globalTick);
    REQUIRE(contextRestore < globalTickGate);
    REQUIRE(globalTickGate < globalTick);
    const size_t dedicatedPolicy = mainTick.find("g_NetDedicated");
    REQUIRE(dedicatedPolicy != std::string::npos);
    REQUIRE(dedicatedPolicy < contextRestore);
    REQUIRE(mainTick.find("(void)playermgrRestoreLocalPlayerContext") ==
        std::string::npos);

    REQUIRE(localPlayer.find("g_NetLocalClient->player") != std::string::npos);
    REQUIRE(localPlayer.find(
        "g_Vars.players[i] == g_NetLocalClient->player") !=
        std::string::npos);
    REQUIRE(localPlayer.find("g_Vars.players[i]->isremote") !=
        std::string::npos);
    REQUIRE(localPlayer.find("localplayernum >= 0") !=
        std::string::npos);
    REQUIRE(localPlayer.find("g_NetLocalClient->playernum") ==
        std::string::npos);
    REQUIRE(presentationPlayer.find(
        "g_NetMode == NETMODE_SERVER && g_NetDedicated") !=
        std::string::npos);
    REQUIRE(presentationPlayer.find("g_Vars.currentplayernum") !=
        std::string::npos);
    REQUIRE(presentationPlayer.find("playermgrGetLocalPlayerNum()") !=
        std::string::npos);
    REQUIRE(restore.find("setCurrentPlayerNum(localplayernum)") !=
        std::string::npos);

    const size_t selectPresentation = presentationStart.find(
        "setCurrentPlayerNum(presentationplayernum)");
    const size_t startPresentation = presentationStart.find(
        "committed = playerStartCutscene(animnum)");
    const size_t restoreAmbient = presentationStart.find(
        "setCurrentPlayerNum(previousplayernum)", startPresentation);
    REQUIRE(selectPresentation != std::string::npos);
    REQUIRE(startPresentation != std::string::npos);
    REQUIRE(restoreAmbient != std::string::npos);
    REQUIRE(selectPresentation < startPresentation);
    REQUIRE(startPresentation < restoreAmbient);
    REQUIRE(presentationStart.find("CUTSCENE.PRESENTATION: start") !=
        std::string::npos);
    REQUIRE(presentationStart.find("return committed") != std::string::npos);
    REQUIRE(presentationStart.find("playermgrGetPresentationPlayerNum()") !=
        std::string::npos);
    REQUIRE(presentationCondition.find(
        "playermgrGetPresentationPlayerNum()") != std::string::npos);
    REQUIRE(presentationCondition.find("g_Vars.currentplayer") ==
        std::string::npos);

    REQUIRE(fallbackStart.find("playerStartCutsceneForPresentation") !=
        std::string::npos);
    REQUIRE(fallbackStart.find("playerStartCutscene(anim_id)") ==
        std::string::npos);
    REQUIRE(graphStart.find("playerStartCutsceneForPresentation") !=
        std::string::npos);
    REQUIRE(graphStart.find("playermgrGetPresentationPlayerNum()") !=
        std::string::npos);
    REQUIRE(graphStart.find(
        "s_aiGraphRequireRuntimePlayerSlot(\"set_camera_animation\"") !=
        std::string::npos);
    REQUIRE(graphStart.find("g_Vars.players[") == std::string::npos);
    REQUIRE(graphStart.find("playermgrGetLocalPlayerNum()") ==
        std::string::npos);
    REQUIRE(graphStart.find("playerStartCutscene((s16)anim_id)") ==
        std::string::npos);
    REQUIRE(fallbackCondition.find(
        "playerPresentationCutsceneInProgress()") != std::string::npos);
    REQUIRE(graphCondition.find(
        "playerPresentationCutsceneInProgress()") != std::string::npos);
    REQUIRE(fallbackChrAnimation.find(
        "playerPresentationCutsceneInProgress()") != std::string::npos);
    REQUIRE(fallbackObjectAnimation.find(
        "playerPresentationCutsceneInProgress()") != std::string::npos);
    REQUIRE(graphChrAnimation.find(
        "playerPresentationCutsceneInProgress()") != std::string::npos);
    REQUIRE(graphObjectAnimation.find(
        "playerPresentationCutsceneInProgress()") != std::string::npos);
}

TEST_CASE("cutscene authority smokes require publication and receiver-local application",
          "[cutscene][network][smoke][v56][b1089][static]")
{
    const std::string initiatorAuthority = readTextFile(
        "tools/smoke-verify/tests/friend_play_authority_initiator_smoke.json");
    const std::string inviteeAuthority = readTextFile(
        "tools/smoke-verify/tests/friend_play_authority_invitee_smoke.json");

    REQUIRE_FALSE(initiatorAuthority.empty());
    REQUIRE_FALSE(inviteeAuthority.empty());

    const std::string *fixtures[] = {&initiatorAuthority, &inviteeAuthority};

    for (const std::string *fixture : fixtures) {
        REQUIRE(fixture->find("authoritative-cutscene-result-published") !=
            std::string::npos);
        REQUIRE(fixture->find("authoritative-cutscene-result-applied") !=
            std::string::npos);
        REQUIRE(fixture->find(
            "NET: CUTSCENE_SKIP\\\\.AUTHORITY accepted source=(?:local|remote)") !=
            std::string::npos);
        REQUIRE(fixture->find(
            "NET: SVC_CUTSCENE_SKIP published requester_mask=0x[0-9a-f]+") !=
            std::string::npos);
        REQUIRE(fixture->find(
            "NET: SVC_CUTSCENE_SKIP applied requester_client=\\\\d+ runtime_player=\\\\d+ generation=\\\\d+") !=
            std::string::npos);
		REQUIRE(fixture->find(
			"NET: SVC_CUTSCENE read active=1 client_mask=0x[0-9a-f]+ runtime_player_mask=0x[0-9a-f]+ generation=\\\\d+") !=
			std::string::npos);
		REQUIRE(fixture->find(
			"NET: SVC_CUTSCENE published active=1 client_mask=0x[0-9a-f]+ generation=\\\\d+ room=\\\\d+") !=
			std::string::npos);
		REQUIRE(fixture->find(
			"NET: SVC_CUTSCENE published active=0 client_mask=0x[0-9a-f]+ generation=\\\\d+ room=\\\\d+") !=
			std::string::npos);
		REQUIRE(fixture->find(
			"NET: SVC_CUTSCENE read active=0 client_mask=0x[0-9a-f]+ runtime_player_mask=0x[0-9a-f]+ generation=\\\\d+") !=
			std::string::npos);
		REQUIRE(fixture->find(
			"CLC_CUTSCENE_SKIP ignored status=generation_mismatch") !=
			std::string::npos);
		REQUIRE(fixture->find(
			"SVC_CUTSCENE_SKIP ignored status=generation_mismatch") !=
			std::string::npos);
		REQUIRE(fixture->find(
			"SVC_CUTSCENE rejected status=") !=
			std::string::npos);
		REQUIRE(fixture->find(
			"consecutive-authority-generations-published") !=
			std::string::npos);
		REQUIRE(fixture->find(
			"consecutive-authority-generations-applied") !=
			std::string::npos);
		REQUIRE(fixture->find(
			"CUTSCENE\\\\.AUTHORITY: tick-mode END queued generation=1") !=
			std::string::npos);
		REQUIRE(fixture->find(
			"generation=2") != std::string::npos);
		REQUIRE(fixture->find(
			"NET: CUTSCENE\\\\.AUTHORITY state queue (?:rejected|full)") !=
			std::string::npos);
        REQUIRE(fixture->find(
            "CUTSCENE\\\\.AUTHORITY: (?:START|END|tick-mode END) preflight rejected") !=
            std::string::npos);
		REQUIRE(fixture->find(
			"CUTSCENE\\\\.PRESENTATION: start local_player=0 ambient_player=\\\\d+ anim=\\\\d+ body_ready=1 committed=1 active=1 in_progress=1") !=
			std::string::npos);
		REQUIRE(fixture->find(
			"CUTSCENE\\\\.PRESENTATION: start local_player=[1-9]") !=
			std::string::npos);
    }
}
