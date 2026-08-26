#include "catch.hpp"

#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>

namespace {
std::string readTextFile(const char *path)
{
    std::ifstream in(path, std::ios::binary);
    REQUIRE(in.good());
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

void requireContains(const std::string &text, const char *needle)
{
    REQUIRE(text.find(needle) != std::string::npos);
}

std::string requireSlice(const std::string &text, const char *begin, const char *end)
{
    const size_t beginPos = text.find(begin);
    REQUIRE(beginPos != std::string::npos);
    const size_t endPos = text.find(end, beginPos + 1);
    REQUIRE(endPos != std::string::npos);
    REQUIRE(beginPos < endPos);
    return text.substr(beginPos, endPos - beginPos);
}
}

TEST_CASE("smoke remove paths are install-relative filesystem paths",
    "[smoke][tooling][fixtures][b1087][static]")
{
    namespace fs = std::filesystem;
    const fs::path root = fs::path(PD_SOURCE_DIR)
        / "tools" / "smoke-verify" / "tests";
    const std::regex arrays(
        R"REGEX("remove_paths"\s*:\s*\[([\s\S]*?)\])REGEX");
    const std::regex strings(R"REGEX("([^"]*)")REGEX");
    size_t files_scanned = 0;
    size_t paths_checked = 0;

    REQUIRE(fs::is_directory(root));
    for (const fs::directory_entry &entry :
            fs::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".json") {
            continue;
        }

        std::ifstream in(entry.path(), std::ios::binary);
        REQUIRE(in.good());
        std::ostringstream out;
        out << in.rdbuf();
        const std::string text = out.str();
        files_scanned++;

        for (std::sregex_iterator array_it(text.begin(), text.end(), arrays), end;
                array_it != end; ++array_it) {
            const std::string body = (*array_it)[1].str();
            for (std::sregex_iterator path_it(body.begin(), body.end(), strings);
                    path_it != end; ++path_it) {
                const std::string relative = (*path_it)[1].str();
                CAPTURE(entry.path().generic_string(), relative);
                REQUIRE_FALSE(relative.empty());
                REQUIRE(relative.find(':') == std::string::npos);

                const fs::path relative_path(relative);
                REQUIRE_FALSE(relative_path.is_absolute());
                for (const fs::path &component : relative_path) {
                    REQUIRE(component != "..");
                }
                paths_checked++;
            }
        }
    }

    REQUIRE(files_scanned > 100);
    REQUIRE(paths_checked > 20);
}

TEST_CASE("smoke input events drive mouse hover and wheel through SDL",
    "[input][menus][smoke][v004][static]")
{
    const std::string source = readTextFile("port/src/smoke_harness.c");

    requireContains(source, "SMOKE_EVENT_MOUSE_MOVE");
    requireContains(source, "SMOKE_EVENT_MOUSE_WHEEL");
    requireContains(source, "!strcmp(type_str, \"mouse_move\")");
    requireContains(source, "!strcmp(type_str, \"mouse_wheel\")");
    requireContains(source, "ev.type = SDL_MOUSEMOTION;");
    requireContains(source, "ev.type = SDL_MOUSEWHEEL;");
    requireContains(source, "static void smokeWarpMouseTo(s32 x, s32 y)");
    requireContains(source, "SDL_WarpMouseInWindow(w, x, y);");
    requireContains(source, "smokeWarpMouseTo(x, y);");
    requireContains(source, "ev.motion.windowID = smokeResolveWindowId();");
    requireContains(source, "ev.wheel.windowID = smokeResolveWindowId();");
    requireContains(source, "ev.wheel.preciseY = (float)wheel_y;");
    requireContains(source, "SMOKE: mouse move xy=(%d,%d) at_ms=%d");
    requireContains(source, "SMOKE: mouse wheel delta=(%d,%d) at_ms=%d");
}

TEST_CASE("smoke mouse motion and wheel reject incomplete no-op events",
    "[input][menus][smoke][v004][static]")
{
    const std::string source = readTextFile("port/src/smoke_harness.c");

    requireContains(source, "SMOKE: mouse_move event missing x/y");
    requireContains(source, "SMOKE: mouse_wheel event missing wheel_x/wheel_y");
    requireContains(source, "SMOKE: mouse_wheel event has zero delta");
}

TEST_CASE("smoke input modes default only when omitted and reject typos",
    "[input][smoke][tooling][b1088][static]")
{
    const std::string source = readTextFile("port/src/smoke_harness.c");

    requireContains(source, "s32  has_action_mode = 0;");
    requireContains(source, "has_action_mode = 1;");
    requireContains(source,
        "else if (!has_action_mode || !strcmp(action_str, \"tap\"))");
    requireContains(source,
        "|| !strcmp(action_str, \"click\"))");
    requireContains(source, "SMOKE: key event has unknown action '%s'");
    requireContains(source, "SMOKE: action event has unknown action '%s'");
    requireContains(source, "SMOKE: mouse event has unknown action '%s'");
    REQUIRE(source.find("tap (default) or any other value") ==
        std::string::npos);
}

TEST_CASE("smoke diagnostics override persisted jump logging per fixture",
    "[smoke][tooling][physics][b1081][static]")
{
    const std::string source = readTextFile("port/src/smoke_harness.c");

    requireContains(source,
        "g_JumpLoggingEnabled = s_State.jump_logging ? 1 : 0;");
    requireContains(source, "if (g_JumpLoggingEnabled) {");
}

TEST_CASE("smoke readiness barriers pause virtual time on production state",
    "[smoke][network][readiness][b1085][b1093][static]")
{
    const std::string source = readTextFile("port/src/smoke_harness.c");
    const std::string readinessProjection = requireSlice(source,
        "static smoke_readiness_facts_t smokeCaptureReadinessFacts",
        "static void smokeLogReadinessWait");
    const std::string header = readTextFile("port/include/smoke_harness.h");
    const std::string policy = readTextFile("port/src/smoke_readiness.c");
    const std::string transition = readTextFile("port/src/smoke_transition.c");
	const std::string fixtureSchema =
		readTextFile("port/src/smoke_fixture_schema.c");

    requireContains(header, "wait_until: bounded typed production-state barrier");
    requireContains(header, "Optional stable_ms requires continuous truth");
    requireContains(header,
        "assist_action/assist_condition/assist_hold_ms tuple owns at");
    requireContains(source, "SMOKE_EVENT_WAIT_UNTIL");
    requireContains(source, "!strcmp(type_str, \"wait_until\")");
    requireContains(source,
        "smokeReadinessConditionFromName(condition_str)");
    requireContains(source,
        "requires timeout_ms in [1,%d]");
    requireContains(source,
        "event at_ms must be in [0,%d]");
    requireContains(source,
        "event cap reached (%d); refusing a partial timeline");
    requireContains(source,
        "if (!smokeParseEvent(&p, ev))");
	requireContains(source, "#include \"smoke_fixture_schema.h\"");
	requireContains(source, "if (!smokeFixtureJsonValid(src))");
	requireContains(source,
		"test JSON is malformed, truncated, or has trailing data");
	requireContains(source, "event has unknown field");
	requireContains(source, "event has duplicate field");
	requireContains(source, "is not supported for event type");
	requireContains(source, "smokeValidateWaitHoldSchedule");
	requireContains(source, "crosses %d explicit input hold(s)");
	requireContains(fixtureSchema, "SMOKE_JSON_MAX_DEPTH 128u");
	requireContains(fixtureSchema, "smokeFixtureEventAllowedFields");
    requireContains(source,
        "assisted wait_until requires assist_action, assist_condition, assist_hold_ms, and stable_ms > 0");
    requireContains(source,
        "assisted wait_until has unknown assist_action");
    requireContains(source,
        "assisted wait_until has unknown assist_condition");
    requireContains(source,
        "assist_hold_ms must be in [1,timeout_ms)");
    requireContains(source, "static int j_tok_is_integer");
    requireContains(source, "event at_ms must be an integer");
    requireContains(source, "wait_until timeout_ms must be an integer");
    requireContains(source, "wait_until stable_ms must be an integer");
    requireContains(source, "wait_until assist_hold_ms must be an integer");
    requireContains(source, "smokeTransitionInit(&s_State.wait_transition");
    requireContains(source, "smokeTransitionTick(&s_State.wait_transition");
    requireContains(source, "SMOKE.WAIT.STABLE:");
    requireContains(source, "SMOKE.WAIT.ASSIST:");
	requireContains(source, "stable_elapsed_ms=%u");
	requireContains(source, "wait_assist_press_count");
	requireContains(source, "wait_assist_release_count");
	requireContains(source, "wait_assist_balance_invalid");
    REQUIRE(source.find("SMOKE.ACTION.CONDITION") == std::string::npos);
    REQUIRE(source.find("cutscene_skip_or_gameplay_ready") ==
        std::string::npos);
    requireContains(source,
        "g_NetLocalClient->state == CLSTATE_GAME");
    requireContains(source, "g_StageNum == g_Vars.stagenum");
    requireContains(source, "sceneInstrumentGet()");
    requireContains(source, "#include \"game/playermgr.h\"");
    requireContains(readinessProjection,
        "const s32 local_player_num = playermgrGetLocalPlayerNum();");
    requireContains(readinessProjection,
        "g_Vars.players[local_player_num]");
    requireContains(source,
        "s_State.ready_stage_num == g_StageNum");
    requireContains(readinessProjection,
        "g_NetLocalClient->player == local_player");
    requireContains(source,
        "scene_layer == LAYER_CUTSCENE");
    requireContains(source,
        "scene_layer == LAYER_GAMEPLAY");
    requireContains(readinessProjection,
        "playerCutsceneInProgress(local_player_num)");
    requireContains(readinessProjection,
        "playerCutsceneCurTotalFrame60f(local_player_num) > 30.0f");
    requireContains(source,
        "g_Vars.tickmode == TICKMODE_NORMAL");
    requireContains(source, "g_Vars.lvupdate240 > 0");
    requireContains(readinessProjection,
        "playerInCutscene(local_player_num)");
    requireContains(readinessProjection,
        "g_PlayersWithControl[local_player_num] != 0");
    REQUIRE(readinessProjection.find("g_Vars.players[0]") ==
        std::string::npos);
    REQUIRE(readinessProjection.find("playerCutsceneInProgress(0)") ==
        std::string::npos);
    REQUIRE(readinessProjection.find("playerCutsceneCurTotalFrame60f(0)") ==
        std::string::npos);
    REQUIRE(readinessProjection.find("playerInCutscene(0)") ==
        std::string::npos);
    REQUIRE(readinessProjection.find("g_PlayersWithControl[0]") ==
        std::string::npos);
    requireContains(source,
        "local_player->pausemode == PAUSEMODE_UNPAUSED");
    requireContains(source, "!local_player->isdead");
    requireContains(source,
        "local_player->bondmovemode == MOVEMODE_WALK");
    requireContains(source,
        "s_State.timeline_pause_ms += waited_ms");
    requireContains(source, "wait_pending_tap");
    requireContains(source, "wait_condition_timeout");
    requireContains(source,
        "if (real_elapsed_ms >= (u32)s_State.timeout_ms)");
    requireContains(source,
        "timeout_seconds must be positive");
    requireContains(source,
        "must exceed schedule bound");
    requireContains(source,
        "SCENE_EVENT_STAGE_TEARDOWN");

    const std::string waitTick = requireSlice(source,
        "static s32 smokeTickReadinessWait", "void smokeHarnessTick");
    const size_t pendingTap = waitTick.find(
        "if (first_tick && smokeHasPendingTap())");
    const size_t policyTick = waitTick.find(
        "plan = smokeTransitionTick(&s_State.wait_transition");
    const size_t assistRelease = waitTick.find(
        "if (plan.release_assist");
    const size_t waitDeadline = waitTick.find("if (plan.timed_out)");
    const size_t waitSuccess = waitTick.find(
        "if (plan.satisfied)");
    REQUIRE(pendingTap != std::string::npos);
    REQUIRE(policyTick != std::string::npos);
    REQUIRE(assistRelease != std::string::npos);
    REQUIRE(waitDeadline != std::string::npos);
    REQUIRE(waitSuccess != std::string::npos);
    REQUIRE(pendingTap < policyTick);
    REQUIRE(policyTick < assistRelease);
    REQUIRE(assistRelease < waitDeadline);
    REQUIRE(waitDeadline < waitSuccess);

    const std::string transitionTick = requireSlice(transition,
        "smoke_transition_plan_t smokeTransitionTick",
        "int smokeTransitionAssistIsHeld");
    const size_t exactDeadline = transitionTick.find(
        "if (elapsed_ms >= config->timeout_ms)");
    const size_t targetObservation = transitionTick.find(
        "if (input->target_met)");
    REQUIRE(exactDeadline != std::string::npos);
    REQUIRE(targetObservation != std::string::npos);
    REQUIRE(exactDeadline < targetObservation);

    const std::string harnessTick = requireSlice(source,
        "void smokeHarnessTick", "void smokeHarnessExit");
    const size_t harnessDeadline = harnessTick.find(
        "if (real_elapsed_ms >= (u32)s_State.timeout_ms)");
    const size_t readinessObserve = harnessTick.find(
        "smokeObserveStageReadyEpoch();");
    const size_t eventDispatch = harnessTick.find(
        "while (s_State.next_event_idx < s_State.event_count)");
    REQUIRE(harnessDeadline != std::string::npos);
    REQUIRE(readinessObserve != std::string::npos);
    REQUIRE(eventDispatch != std::string::npos);
    REQUIRE(harnessDeadline < readinessObserve);
    REQUIRE(harnessDeadline < eventDispatch);

    const std::string harnessExit = requireSlice(source,
        "void smokeHarnessExit", "fflush(NULL);");
    const size_t cleanup = harnessExit.find(
        "if (s_State.wait_assist_injected)");
    const size_t markExited = harnessExit.find("s_State.exited = 1;");
    REQUIRE(cleanup != std::string::npos);
    REQUIRE(markExited != std::string::npos);
    REQUIRE(cleanup < markExited);
    requireContains(policy,
        "case SMOKE_READINESS_NETWORK_STAGE_LIVE:");
    requireContains(policy,
        "case SMOKE_READINESS_CUTSCENE_SKIP_READY:");
    requireContains(policy,
        "case SMOKE_READINESS_GAMEPLAY_READY:");
	requireContains(policy,
		"case SMOKE_READINESS_OFFLINE_GAMEPLAY_READY:");
	requireContains(policy,
		"case SMOKE_READINESS_ENDSCREEN_VISIBLE:");
    requireContains(policy, "&& facts->stage_ready_epoch");
    requireContains(policy, "&& facts->cutscene_frame_ready");
	requireContains(policy, "&& facts->cutscene_authority_ready");
	requireContains(source,
		"facts.cutscene_authority_ready = playerCutsceneGeneration() != 0;");
	requireContains(readinessProjection,
		"!facts.network_active || (g_NetLocalClient");
	requireContains(readinessProjection,
		"menupoolIsActive(MENU_TYPE_ENDSCREEN_MP)");
	requireContains(readinessProjection,
		"inputCtxIsActive(&g_CtxImGuiMenu)");
    requireContains(policy, "&& facts->scene_gameplay_layer");
    requireContains(policy, "&& facts->gameplay_updates_active");
    requireContains(policy, "&& !facts->player_in_cutscene");
    requireContains(policy, "&& facts->player_has_control");
    requireContains(policy, "&& facts->player_unpaused");
    requireContains(policy, "&& facts->player_alive");
    requireContains(policy, "&& facts->player_walk_mode");
    REQUIRE(policy.find("SMOKE_READINESS_CUTSCENE_SKIP_OR_GAMEPLAY_READY") ==
        std::string::npos);
}

TEST_CASE("smoke-owned action reads bypass only gameplay focus suppression",
    "[input][smoke][tooling][b1085][static]")
{
    const std::string header = readTextFile("port/include/actionmap.h");
    const std::string source = readTextFile("port/src/actionmap.cpp");
    const std::string authority = readTextFile(
        "port/src/action_read_authority.c");

    requireContains(header, "s32  smoke_injected;");
    requireContains(source, "SMOKE_ACTION_NONE = 0");
    requireContains(source, "SMOKE_ACTION_HELD = 1");
    requireContains(source, "SMOKE_ACTION_RELEASED = 2");
    requireContains(source,
        "static s32 actionLayerApertureDecision(InputAction a)");
    requireContains(source,
        "static s32 actionReadAllows(s32 player, InputAction a)");
    requireContains(source, "#include \"action_read_authority.h\"");
    requireContains(source,
        "input.aperture_decision = actionLayerApertureDecision(a);");
    requireContains(source,
        "input.smoke_owned = actionStateIsSmokeOwned(player, a);");
    requireContains(source,
        "input.gameplay_context = context == &g_CtxGameplay || context == &g_CtxForgeEditor;");
    requireContains(source, "inputCtxDebugSnapshotAuthority(&authority);");
    requireContains(source,
        "input.focus_settle_active = authority.focus_settle_remaining_ms > 0;");
    requireContains(authority,
        "return input->smoke_owned &&\n        input->gameplay_context &&");

    /* Pin read-authority ordering inside the helper: typed aperture first,
     * ordinary suppression next, explicit ownership next, then a gameplay
     * context plus focus-only reason. */
    const std::string readGate = requireSlice(source,
        "static s32 actionReadAllows(s32 player, InputAction a)",
        "/* Priority D (2026-04-24): FREEFLY observer suppression.");
    const size_t aperture = readGate.find("actionLayerApertureDecision(a)");
    const size_t suppression = readGate.find("gameplayInputSuppressed()");
    const size_t ownership = readGate.find("actionStateIsSmokeOwned(player, a)");
    const size_t context = readGate.find(
        "input.gameplay_context = context == &g_CtxGameplay || context == &g_CtxForgeEditor;");
    const size_t focus = readGate.find("input.focus_lost = authority.window_focus_lost;");
    REQUIRE(aperture != std::string::npos);
    REQUIRE(suppression != std::string::npos);
    REQUIRE(ownership != std::string::npos);
    REQUIRE(context != std::string::npos);
    REQUIRE(focus != std::string::npos);
    REQUIRE(aperture < suppression);
    REQUIRE(suppression < ownership);
    REQUIRE(ownership < context);
    REQUIRE(context < focus);

    /* Smoke injection cannot seize already-held physical state, and an
     * unowned release cannot clear or claim it. */
    const std::string inject = requireSlice(source,
        "s32 actionmapInjectStateForSmoke(s32 player, s32 action, s32 down)",
        "return 1;\n}");
    const size_t ownershipCollision = inject.find(
        "smoke_injected_before == SMOKE_ACTION_NONE && held_before");
    const size_t acquireOwnership = inject.find(
        "st->smoke_injected = SMOKE_ACTION_HELD;");
    const size_t ownedRelease = inject.find(
        "else if (smoke_injected_before == SMOKE_ACTION_HELD)");
    const size_t releaseMutation = inject.find("st->held       = 0;", ownedRelease);
    const size_t unownedRelease = inject.find(
        "An idempotent release cannot seize or mutate physically owned", ownedRelease);
    REQUIRE(ownershipCollision != std::string::npos);
    REQUIRE(acquireOwnership != std::string::npos);
    REQUIRE(ownedRelease != std::string::npos);
    REQUIRE(releaseMutation != std::string::npos);
    REQUIRE(unownedRelease != std::string::npos);
    REQUIRE(ownershipCollision < acquireOwnership);
    REQUIRE(ownedRelease < releaseMutation);
    REQUIRE(releaseMutation < unownedRelease);

    /* Physical dispatch keeps the production layer gate and then defers only
     * for the exact smoke-owned action before any button state write. */
    const std::string fireVk = requireSlice(source,
        "static void fireVk(u32 vk, s32 is_down)",
        "static void handleAxisDigital(s32 player, s16 val,");
    const size_t physicalGate = fireVk.find(
        "if (!actionLayerAllows((InputAction)best_a))");
    const size_t stateLookup = fireVk.find(
        "ActionState *st = &s_State[player][best_a]");
    const size_t ownedGuard = fireVk.find(
        "if (actionStateIsSmokeOwned(player, (InputAction)best_a))");
    const size_t stateWrite = fireVk.find("if (is_down)");
    REQUIRE(physicalGate != std::string::npos);
    REQUIRE(stateLookup != std::string::npos);
    REQUIRE(ownedGuard != std::string::npos);
    REQUIRE(stateWrite != std::string::npos);
    REQUIRE(physicalGate < stateLookup);
    REQUIRE(stateLookup < ownedGuard);
    REQUIRE(ownedGuard < stateWrite);

    /* Analog polling and focus/menu zeroing route through the ownership-aware
     * physical setter; no direct axis assignment can overwrite an injected
     * axis. The production layer gate itself remains unchanged. */
    const std::string physicalAxisSetter = requireSlice(source,
        "static void actionmapSetPhysicalAxisState(s32 player, InputAction action, f32 value)",
        "/* M-2: Pre-computed list of actions bound to mouse wheel VKs");
    const size_t axisOwnedGuard = physicalAxisSetter.find(
        "actionStateIsSmokeOwned(player, action)");
    const size_t axisStateLookup = physicalAxisSetter.find(
        "ActionState *st = &s_State[player][action]");
    const size_t axisValueWrite = physicalAxisSetter.find("st->value = value;");
    REQUIRE(axisOwnedGuard != std::string::npos);
    REQUIRE(axisStateLookup != std::string::npos);
    REQUIRE(axisValueWrite != std::string::npos);
    REQUIRE(axisOwnedGuard < axisStateLookup);
    REQUIRE(axisStateLookup < axisValueWrite);

    const std::string zeroAxes = requireSlice(source,
        "static void actionmapZeroGameplayAxes(s32 player, s32 zero_move, s32 zero_aim)",
        "/* ============================================================\n * Public: actionmapPollFrame");
    requireContains(zeroAxes,
        "actionmapSetPhysicalAxisState(player, ACTION_AXIS_MOVE_X, 0.0f)");
    requireContains(zeroAxes,
        "actionmapSetPhysicalAxisState(player, ACTION_AXIS_AIM_Y, 0.0f)");
    REQUIRE(zeroAxes.find("s_State[player][ACTION_AXIS_") == std::string::npos);

    const std::string poll = requireSlice(source,
        "void actionmapPollFrame(void)",
        "/* ============================================================\n * Public: actionmapEndFrame");
    requireContains(poll, "actionLayerAllows(ACTION_AXIS_MOVE_X)");
    requireContains(poll, "actionLayerAllows(ACTION_AXIS_AIM_Y)");
    requireContains(poll,
        "actionmapSetPhysicalAxisState(p, ACTION_AXIS_MOVE_X,");
    requireContains(poll,
        "actionmapSetPhysicalAxisState(0, ACTION_AXIS_AIM_Y, dy)");
    REQUIRE(poll.find("s_State[p][ACTION_AXIS_MOVE_X].value =") ==
        std::string::npos);
    REQUIRE(poll.find("s_State[0][ACTION_AXIS_AIM_X].value =") ==
        std::string::npos);

    /* Ownership is lifecycle-bound: flush clears it, release persists for
     * one readable edge frame, and end-of-frame retires that edge. */
    requireContains(source,
        "st->smoke_injected = SMOKE_ACTION_NONE;");
    requireContains(source,
        "if (st->smoke_injected == SMOKE_ACTION_RELEASED)");
    requireContains(source,
        "st->smoke_injected = SMOKE_ACTION_HELD;");
    requireContains(source,
        "st->smoke_injected = SMOKE_ACTION_RELEASED;");

    const std::string endFrame = requireSlice(source,
        "void actionmapEndFrame(void)",
        "/* ============================================================\n * Input authority: classification + flush helpers");
    const size_t retireRelease = endFrame.find(
        "if (st->smoke_injected == SMOKE_ACTION_RELEASED)");
    const size_t wheelRelease = endFrame.find(
        "if (st->smoke_injected == SMOKE_ACTION_HELD)");
    REQUIRE(retireRelease != std::string::npos);
    REQUIRE(wheelRelease != std::string::npos);
    REQUIRE(retireRelease < wheelRelease);

    const std::string flush = requireSlice(source,
        "static void actionmapFlushStateSlot(ActionState *st, u32 now)",
        "void actionmapFlushGameplayState(void)");
    requireContains(flush, "st->smoke_injected = SMOKE_ACTION_NONE;");

    /* Query reads use the player-aware gate and retain FREEFLY blocking. */
    requireContains(source,
        "if (!actionReadAllows(player, action)) return 0;");
    requireContains(source,
        "forgeIsFreefly() && actionIsBlockedInFreefly(action)");
    requireContains(source,
        "actionReadAllows(player, ACTION_AXIS_MOVE_X)");
    requireContains(source,
        "actionReadAllows(player, ACTION_AXIS_MOVE_Y)");
}

TEST_CASE("invitee authority route smoke isolates D-003 from focus and visual gates",
    "[network][smoke][d-003][route-only][static]")
{
    const std::string fixture = readTextFile(
        "tools/smoke-verify/tests/friend_play_authority_invitee_route_smoke.json");
    const std::string invitee = requireSlice(fixture,
        "\"name\": \"invitee\"", "\"name\": \"initiator\"");
    const std::string initiator = requireSlice(fixture,
        "\"name\": \"initiator\"", "\"input_sequence\"");

    requireContains(fixture,
        "\"scenario_name\": \"friend_play_authority_invitee_route_smoke\"");
    requireContains(fixture, "\"separate_process_installs\": true");
    requireContains(fixture, "\"friend_play_identity\": \"invitee\"");
    requireContains(fixture, "\"friend_play_identity\": \"initiator\"");
    requireContains(fixture, "base:arena_mp_felicity");
    requireContains(fixture,
        "GROUP.SESSION: authority elected handle=0x73eb8f71 kbps=9000 local=1");
    requireContains(fixture,
        "GROUP.SESSION: authority elected handle=0x73eb8f71 kbps=9000 local=0");
    requireContains(invitee,
        "GROUP.MATCH: server start attempt authority=0x73eb8f71 count=1");
    requireContains(invitee,
        "GROUP.MATCH: authority latched handle=0x73eb8f71 transport=listen-host");
    requireContains(invitee,
        "GROUP.SESSION: published typed match route authority=0x73eb8f71 flags=0x[0-9a-f]+ port=27210");
    requireContains(initiator,
        "PRESENCE: accepted signed match route authority=0x73eb8f71 flags=0x[0-9a-f]+ port=27210 fresh=1");
    requireContains(initiator,
        "GROUP.MATCH: join attempt authority=0x73eb8f71 route_kind=match-server flags=0x[0-9a-f]+ count=1");
    requireContains(initiator,
        "GROUP.MATCH: authority latched handle=0x73eb8f71 transport=client route_flags=0x[0-9a-f]+");
    requireContains(fixture, "\"invitee-authority-start-and-publish\"");
    requireContains(fixture, "\"initiator-consumes-only-signed-match-route\"");
    requireContains(fixture, "\"v58-release-after-authority-and-peer-ready\"");
    requireContains(fixture,
        "NET.STAGE.REPLICATION phase=waiting epoch=[1-9]\\\\d*");
    requireContains(fixture,
        "NET.STAGE.REPLICATION ready authority=server epoch=[1-9]\\\\d* duplicate=0 post_load=1");
    requireContains(fixture,
        "NET.STAGE.REPLICATION phase=release epoch=[1-9]\\\\d*");
    requireContains(fixture,
        "NET.STAGE.REPLICATION phase=active epoch=[1-9]\\\\d*");
    requireContains(fixture,
        "\"condition\": \"network_stage_live\", \"timeout_ms\": 220000");
    requireContains(fixture,
        "\"condition\": \"gameplay_ready\", \"timeout_ms\": 60000, \"stable_ms\": 3000");
    requireContains(fixture,
        "probe endpoint.*netStartClient");
    requireContains(fixture,
        "relay descriptor.*netStartClient");
    requireContains(fixture,
        "P2P\\\\.(LAN|STUN|UPNP|ICE).*netStartClient");
    requireContains(fixture,
        "GROUP.SESSION: auxiliary probe.*netStartClient");
    requireContains(fixture,
        "\"pattern\": \"GROUP.MATCH: server start attempt authority=0x73eb8f71 count=1\", \"min\": 1, \"max\": 1");
    requireContains(fixture,
        "\"pattern\": \"GROUP.MATCH: join attempt authority=0x73eb8f71 route_kind=match-server flags=0x[0-9a-f]+ count=1\", \"min\": 1, \"max\": 1");
    requireContains(fixture,
        "\"pattern\": \"SMOKE: result=scripted_exit\", \"min\": 2, \"max\": 2");
    requireContains(fixture,
        "\"pattern\": \"SMOKE\\\\.WAIT: satisfied condition=gameplay_ready\", \"min\": 2, \"max\": 2");

    /* B-1085 and V-009 retain their unchanged integrated fixture. This route
     * proof must never acquire a focus transition, direct fire, generated
     * rendering, effect audit, package distribution, or Needler dependency. */
    REQUIRE(fixture.find("window_focus_transition") == std::string::npos);
    REQUIRE(fixture.find("assist_action") == std::string::npos);
    REQUIRE(fixture.find("ACTION_FIRE") == std::string::npos);
    REQUIRE(fixture.find("debug-generated-mesh-render-audit") ==
        std::string::npos);
    REQUIRE(fixture.find("debug-effect-runtime-audit") == std::string::npos);
    REQUIRE(fixture.find("packed_fixtures") == std::string::npos);
    REQUIRE(fixture.find("needler") == std::string::npos);
    REQUIRE(fixture.find(
        "ready authority=server epoch=[1-9]\\\\d+ duplicate=0") ==
        std::string::npos);
}

TEST_CASE("invitee authority smoke proves background owned fire reaches gameplay",
    "[input][network][smoke][d-003][b1085][static]")
{
    const std::string fixture = readTextFile(
        "tools/smoke-verify/tests/friend_play_authority_invitee_smoke.json");
    const std::string runner = readTextFile("tools/smoke-verify/run.ps1");
    const std::string assertions = readTextFile(
        "tools/smoke-verify/lib/Test-Assertions.ps1");
    const std::string assertionTests = readTextFile(
        "tools/smoke-verify/test-assertions.ps1");
    const std::string invitee = requireSlice(fixture,
        "\"name\": \"invitee\"", "\"name\": \"initiator\"");
    const std::string initiator = requireSlice(fixture,
        "\"name\": \"initiator\"", "\"input_sequence\"");

    /* The elected invitee is the background window after the initiator boots.
     * Require the production receipt to prove that focus was actually lost,
     * the harness claimed the exact fire action, and gameplay observed it as
     * held. Route/effect assertions alone can pass without exercising B-1085. */
    requireContains(invitee, "INPUTCTX: focus LOST");
    requireContains(invitee,
        "SMOKE\\\\.ACTION\\\\.INJECT: player=0 action=20 down=1 held_before=0 pressed_before=0 released_before=0 held_after=1 pressed_after=1 released_after=0 smoke_owner_before=0 smoke_owner_after=1");
    requireContains(invitee,
        "LOG\\\\.WPN\\\\.DIAG: fire-input pi=0 fire_held=1");
    requireContains(invitee, "\"required_sequences\"");
    requireContains(invitee, "\"focus-loss-to-owned-fire-read\"");
    requireContains(invitee, "\"focus-loss-to-owned-fire-effects\"");
    const std::string preFocusSequences = requireSlice(invitee,
        "\"name\": \"stable-gameplay-transition\"",
        "\"name\": \"focus-loss-to-owned-fire-read\"");
    const std::string focusReadSequence = requireSlice(invitee,
        "\"name\": \"focus-loss-to-owned-fire-read\"",
        "\"name\": \"focus-loss-to-owned-fire-effects\"");
    const std::string focusEffectsSequence = requireSlice(invitee,
        "\"name\": \"focus-loss-to-owned-fire-effects\"",
        "\"forbidden_patterns\"");
    REQUIRE(preFocusSequences.find("\"anchor\"") == std::string::npos);
    requireContains(focusReadSequence,
        "\"anchor\": \"window_focus_transition\"");
    requireContains(focusEffectsSequence,
        "\"anchor\": \"window_focus_transition\"");

    /* Ordered sequences are a reusable assertion family with behavioral
     * proof for success, reversal, strict-later matching, and malformed input. */
    requireContains(assertions, "required_sequences");
    requireContains(assertions, "required_sequence_mismatch");
    requireContains(assertions, "required_sequence_anchor_missing");
    requireContains(assertions, "$RequiredSequenceStartLines.Contains($anchorName)");
    requireContains(assertions, "$cursor = $hitLine + 1");
    requireContains(assertionTests, "causal-reversal");
    requireContains(assertionTests, "strict-later-line");
    requireContains(assertionTests,
        "sequence anchor must ignore a complete sequence that began earlier");
    requireContains(assertionTests,
        "anchored first pattern must not search forward from an earlier line");
    requireContains(assertionTests,
        "named anchor should scope only its tagged sequence");
    requireContains(assertionTests,
        "missing named anchor must fail closed");
    requireContains(assertionTests,
        "unreachable named anchor must reject stale causal evidence");
    requireContains(assertionTests, "malformed sequence must fail closed");

    /* Launch order cannot prove Windows focus ownership. Resolve one visible,
     * unowned top-level HWND by exact PID, establish GUI-thread focus with
     * checked supported APIs, and require fresh SDL gained then lost witnesses.
     * Native focus alone cannot prove the selected HWND is SDL's game window. */
    requireContains(fixture, "\"window_focus_transition\"");
    requireContains(fixture, "\"from_process\": \"invitee\"");
    requireContains(fixture, "\"to_process\": \"initiator\"");
    requireContains(fixture,
        "\"source_wait_for\": \"INPUTCTX: focus GAINED\"");
    requireContains(fixture,
        "\"wait_for\": \"INPUTCTX: focus LOST\"");
    /* The invitee authority may spend a bounded minute in route discovery
     * after FRIENDPLAY readiness. Wait until the second peer has crossed the
     * authoritative cutscene and stable-gameplay gate before moving Windows
     * focus, so this B-1085 proof cannot race D-003/B-1089 startup work. */
    requireContains(initiator,
        "\"wait_for\": \"SMOKE\\\\.WAIT: satisfied condition=gameplay_ready\"");
    requireContains(initiator, "\"wait_timeout_seconds\": 350");
    const std::string transition = requireSlice(runner,
        "function Invoke-MultiProcessWindowFocusTransition",
        "function Invoke-SmokeTestMultiProcess");
    const std::string witnessHelper = requireSlice(runner,
        "function Wait-AppendOnlyLogWitness",
        "function Invoke-MultiProcessWindowFocusTransition");
    const size_t monotonicBudget = transition.find(
        "$transitionClock = [System.Diagnostics.Stopwatch]::StartNew()");
    const size_t uniqueWindow = transition.find(
        "[PdSmokeWindowCapture]::ResolveUniqueProcessWindow(");
    const size_t baseline = transition.find(
        "$baselineText = [System.IO.File]::ReadAllText($fromEntry.LogPath)");
    const size_t focusFrom = transition.find(
        "[PdSmokeWindowCapture]::Focus($fromHandle, $remainingFocusMs)");
    const size_t sourceWitness = transition.find(
        "$sourceWitness = Wait-AppendOnlyLogWitness", focusFrom);
    const size_t sourceInputRecheck = transition.find(
        "[PdSmokeWindowCapture]::HasInputFocus($fromHandle)", sourceWitness);
    const size_t focusTo = transition.find(
        "[PdSmokeWindowCapture]::Focus($toHandle, $remainingFocusMs)");
    const size_t secondBaseline = transition.find(
        "$transitionBaselineText = [System.IO.File]::ReadAllText($fromEntry.LogPath)");
    const size_t lostWitness = transition.find(
        "$lostWitness = Wait-AppendOnlyLogWitness", focusTo);
    const size_t targetRecheck = transition.find(
        "[PdSmokeWindowCapture]::HasInputFocus($toHandle)", lostWitness);
    const size_t witnessLine = transition.find(
        "$fromEntry.RequiredSequenceStartLines['window_focus_transition'] =",
        targetRecheck);
    REQUIRE(monotonicBudget != std::string::npos);
    REQUIRE(uniqueWindow != std::string::npos);
    REQUIRE(baseline != std::string::npos);
    REQUIRE(focusFrom != std::string::npos);
    REQUIRE(sourceWitness != std::string::npos);
    REQUIRE(sourceInputRecheck != std::string::npos);
    REQUIRE(focusTo != std::string::npos);
    REQUIRE(secondBaseline != std::string::npos);
    REQUIRE(lostWitness != std::string::npos);
    REQUIRE(targetRecheck != std::string::npos);
    REQUIRE(witnessLine != std::string::npos);
    REQUIRE(monotonicBudget < uniqueWindow);
    REQUIRE(uniqueWindow < baseline);
    REQUIRE(baseline < focusFrom);
    REQUIRE(focusFrom < sourceWitness);
    REQUIRE(sourceWitness < sourceInputRecheck);
    REQUIRE(focusFrom < sourceInputRecheck);
    REQUIRE(sourceInputRecheck < secondBaseline);
    REQUIRE(focusFrom < secondBaseline);
    REQUIRE(secondBaseline < focusTo);
    REQUIRE(focusFrom < focusTo);
    REQUIRE(focusTo < lostWitness);
    REQUIRE(lostWitness < targetRecheck);
    REQUIRE(targetRecheck < witnessLine);
    requireContains(witnessHelper, "$currentText.StartsWith(");
    requireContains(witnessHelper, "$currentText.Substring($BaselineText.Length)");
    requireContains(witnessHelper, "$skipFirstCompleteLine");
    requireContains(witnessHelper, "$completeLineRegex.Matches($delta)");
    requireContains(witnessHelper, "$absoluteWitnessOffset");
    requireContains(runner, "using System.Diagnostics;");
    requireContains(runner,
        "public static bool Focus(IntPtr hWnd, int timeoutMs)");
    requireContains(runner, "Stopwatch elapsed = Stopwatch.StartNew();");
    requireContains(runner, "public struct GUITHREADINFO");
    requireContains(runner,
        "private static extern bool GetGUIThreadInfo(uint idThread, ref GUITHREADINFO guiInfo);");
    requireContains(runner, "public static bool HasInputFocus(IntPtr hWnd)");
    requireContains(runner, "guiInfo.hwndActive == hWnd");
    requireContains(runner, "guiInfo.hwndFocus == hWnd");
    requireContains(runner,
        "public static IntPtr ResolveUniqueProcessWindow(uint processId)");
    requireContains(runner, "private static extern bool EnumWindows(");
    requireContains(runner, "actualProcessId != processId");
    requireContains(runner, "GetWindow(hWnd, GW_OWNER) != IntPtr.Zero");
    requireContains(runner,
        "return candidates.Count == 1 ? candidates[0] : IntPtr.Zero;");
    requireContains(runner, "private static extern IntPtr SetActiveWindow");
    requireContains(runner, "attach_target={0}/error={1}");
    requireContains(runner, "public static string LastFocusDiagnostics");
    REQUIRE(runner.find("SwitchToThisWindow") == std::string::npos);
    REQUIRE(transition.find(".Process.MainWindowHandle") == std::string::npos);
    requireContains(witnessHelper,
        "The pre-existing partial line may be completed after the snapshot");
    requireContains(runner,
        "$focusTransitionPassed = Invoke-MultiProcessWindowFocusTransition");
    requireContains(runner, "RequiredSequenceStartLines = @{}");
    requireContains(runner,
        "-RequiredSequenceStartLines $entry.RequiredSequenceStartLines");

    /* An operational focus failure must invalidate causal evidence, not only
     * flip the aggregate result while old whole-log sequences still count. */
    const std::string multi = requireSlice(runner,
        "function Invoke-SmokeTestMultiProcess",
        "function Invoke-SmokeTest {");
    const size_t transitionCall = multi.find(
        "$focusTransitionPassed = Invoke-MultiProcessWindowFocusTransition");
    const size_t operationalFailure = multi.find(
        "$operationalFailures.Add($script:lastWindowFocusTransitionFailure)",
        transitionCall);
    const size_t failureSentinel = multi.find(
        "$entry.RequiredSequenceStartLines['window_focus_transition'] =",
        transitionCall);
    const size_t launchFailure = multi.find("$launchFailed = $true",
        transitionCall);
    REQUIRE(transitionCall != std::string::npos);
    REQUIRE(operationalFailure != std::string::npos);
    REQUIRE(failureSentinel != std::string::npos);
    REQUIRE(launchFailure != std::string::npos);
    REQUIRE(transitionCall < operationalFailure);
    REQUIRE(operationalFailure < failureSentinel);
    REQUIRE(failureSentinel < launchFailure);
    requireContains(runner,
        "A failed operational transition invalidates every sequence explicitly");
    requireContains(runner, "Kind = 'window_focus_transition_failed'");
    requireContains(runner, "OperationalFailures = @($operationalFailures)");
}

TEST_CASE("main-window focus lifecycle bypasses consumable UI event routing",
    "[input][window][smoke][b1085][static]")
{
    const std::string source = readTextFile("port/fast3d/gfx_sdl2.cpp");
    const std::string route = requireSlice(source,
        "static void gfx_sdl_route_main_window_focus(const SDL_Event *event)",
        "static void gfx_sdl_reconcile_main_window_focus(void)");
    const std::string reconcile = requireSlice(source,
        "static void gfx_sdl_reconcile_main_window_focus(void)",
        "static void gfx_sdl_handle_events(void)");
    const std::string loop = requireSlice(source,
        "static void gfx_sdl_handle_events(void)",
        "static bool gfx_sdl_start_frame(void)");

    /* Focus authority belongs to the main gameplay window and must run before
     * a UI/input context can return consumed. ImGui still receives the same
     * event afterward for its own bookkeeping. */
    requireContains(route, "event->type != SDL_WINDOWEVENT");
    requireContains(route,
        "event->window.windowID != SDL_GetWindowID(wnd)");
    requireContains(route, "SDL_WINDOWEVENT_FOCUS_LOST");
    requireContains(route, "inputCtxNotifyFocus(0);");
    requireContains(route, "SDL_WINDOWEVENT_FOCUS_GAINED");
    requireContains(route, "inputCtxNotifyFocus(1);");

    const size_t routeCall = loop.find(
        "gfx_sdl_route_main_window_focus(&event);");
    const size_t uiDispatch = loop.find("pdguiProcessEvent(&event)");
    const size_t consumedBranch = loop.find("if (consumed)", uiDispatch);
    REQUIRE(routeCall != std::string::npos);
    REQUIRE(uiDispatch != std::string::npos);
    REQUIRE(consumedBranch != std::string::npos);
    REQUIRE(routeCall < uiDispatch);
    REQUIRE(uiDispatch < consumedBranch);

    /* Event delivery is not the sole source of truth: once each pump, the
     * idempotent input authority reconciles SDL's actual main-window flag. */
    requireContains(reconcile, "SDL_GetWindowFlags(wnd)");
    requireContains(reconcile, "SDL_WINDOW_INPUT_FOCUS");
    requireContains(reconcile, "inputCtxNotifyFocus(");
    const size_t reconcileCall = loop.find(
        "gfx_sdl_reconcile_main_window_focus();", consumedBranch);
    REQUIRE(reconcileCall != std::string::npos);
    REQUIRE(consumedBranch < reconcileCall);

    /* The post-UI switch must not notify a second time. */
    const std::string postUiSwitch = requireSlice(loop,
        "switch (event.type)",
        "gfx_sdl_reconcile_main_window_focus();");
    REQUIRE(postUiSwitch.find("SDL_WINDOWEVENT_FOCUS_LOST") ==
        std::string::npos);
    REQUIRE(postUiSwitch.find("SDL_WINDOWEVENT_FOCUS_GAINED") ==
        std::string::npos);
    REQUIRE(postUiSwitch.find("inputCtxNotifyFocus") == std::string::npos);
}

TEST_CASE("smoke can deliver received archives after a live owner activates",
    "[catalog][network][smoke][v009][b1027][static]")
{
    const std::string source = readTextFile("port/src/smoke_harness.c");
    const std::string network = readTextFile("port/src/net/netdistrib.c");

    requireContains(source, "SMOKE_EVENT_RECEIVE_PDCA_LIST");
    requireContains(source, "!strcmp(type_str, \"receive_pdca_list\")");
    requireContains(source, "netDistribDebugReceivePdcaListForSmoke(ev->path)");
    requireContains(source, "SMOKE: receive_pdca_list path='%s' delivered=%d");
    requireContains(network, "pdcaExtractTransactionRollback(&install_transaction)");
    requireContains(network, "catalogReloadInvalidatedTypedAssets()");
    requireContains(network, "DISTRIB.CATALOG.RELOAD: id=%s result=%d");
}

TEST_CASE("smoke can exercise Agent replacement rollback and active deletion",
    "[save][agent][smoke][d-005][static]")
{
    const std::string source = readTextFile("port/src/smoke_harness.c");
    const std::string scenario = readTextFile(
        "tools/smoke-verify/tests/agent_profile_failure_rollback_smoke.json");

    requireContains(source, "SMOKE_EVENT_AGENT_ACTIVATE");
    requireContains(source, "SMOKE_EVENT_AGENT_DELETE");
    requireContains(source, "!strcmp(type_str, \"agent_activate\")");
    requireContains(source, "!strcmp(type_str, \"agent_delete\")");
    requireContains(source, "agentSessionActivate(ev->path)");
    requireContains(source, "agentSessionDelete(ev->path)");
    requireContains(source, "active_before='%s' active_after='%s'");
    requireContains(scenario, "\"type\": \"agent_activate\"");
    requireContains(scenario, "\"type\": \"agent_delete\"");
}

TEST_CASE("Needler replacement smoke generates valid then invalid same-slot PDCA",
    "[catalog][network][modding][v009][b1027][static]")
{
    const std::string generator = readTextFile(
        "devtools/generate-needler-effect-replacement-fixtures.py");
    const std::string runner = readTextFile("tools/smoke-verify/run.ps1");
    const std::string scenario = readTextFile(
        "tools/smoke-verify/tests/needler_effect_replacement_rollback_smoke.json");

    requireContains(generator, "burst_tint=(0.0, 1.0, 1.0, 1.0)");
    requireContains(generator, "mod_needler:missing_replacement_sfx");
    requireContains(generator,
        "f\"{pdca_relative}|needler_effect|received|1\\n\"");
    requireContains(runner, "needler_effect_replacement_fixtures");
    requireContains(runner, "New-NeedlerEffectReplacementFixtures");
    requireContains(runner, "[System.IO.Path]::GetRelativePath(");
    requireContains(runner,
        "devtools/generate-needler-effect-replacement-fixtures.py");
    requireContains(scenario, "\"type\": \"receive_pdca_list\"");
    requireContains(scenario, "valid-list.txt");
    requireContains(scenario, "invalid-list.txt");
    requireContains(scenario,
        "DISTRIB\\\\.CATALOG\\\\.RELOAD: id=needler_effect result=1");
}

TEST_CASE("smoke runner stages corrupt selected public source fixtures",
    "[smoke][tooling][v006][b960][static]")
{
    const std::string runner = readTextFile("tools/smoke-verify/run.ps1");
    const std::string scenario = readTextFile(
        "tools/smoke-verify/tests/v006_selected_source_failclosed_smoke.json");
    const std::string generator = readTextFile(
        "devtools/generate-v006-corrupt-source-fixtures.py");

    requireContains(runner, "v006_corrupt_source_fixtures");
    requireContains(runner, "generate-v006-corrupt-source-fixtures.py");
    requireContains(scenario, "--debug-load-catalog-assets-source-only");
    requireContains(scenario, "--debug-probe-animation-source-only");
    requireContains(scenario, "--debug-play-catalog-audio-source-only");
    requireContains(scenario, "v006:corrupt_mp3");
    requireContains(generator, "not-an-mp3-file!");
    requireContains(generator, "not-an-sfnt-face");
    requireContains(generator, "not-a-wave");
    requireContains(generator, "{broken-json");

    const std::string main = readTextFile("port/src/main.c");
    requireContains(main, "modTextureLoadRgba32Source");
    requireContains(main, "prior_not_load_mod = g_NotLoadMod");
    requireContains(main, "modSequenceVirtualTrackForCatalogId(asset_id)");
    requireContains(main, "pdguiFontModValidateCatalogId");
    requireContains(main,
        "--debug-load-catalog-assets consumer type=font id='%s' result=FAIL");
}

TEST_CASE("V006 save smoke drives exact atomic JSON and binary failure paths",
    "[smoke][tooling][v006][b1046][static]")
{
    const std::string scenario = readTextFile(
        "tools/smoke-verify/tests/v006_save_failclosed_smoke.json");
    const std::string main = readTextFile("port/src/main.c");
    const std::string harness = readTextFile("port/src/v006_save_harness.c");

    requireContains(scenario, "--debug-v006-save-failclosed");
    requireContains(scenario, "json_semantic_atomic_reject");
    requireContains(scenario, "binary_truncated_atomic_reject");
    requireContains(scenario, "binary_failed_commit_preserves_file");
    requireContains(main, "v006SaveFailclosedRun()");
    requireContains(harness, "saveLoadMpSetup(\"v006_atomic\")");
    requireContains(harness, "mpsetupLoadCurrentFile()");
    requireContains(harness, "saveAtomicDebugFailNextCommit()");
    requireContains(harness, "memcmp(&before, &g_MpSetupFile, sizeof(before))");
}

TEST_CASE("smoke runner filters auxiliary pipeline output before summary",
    "[smoke][tooling][static][b1029]")
{
    const std::string runner = readTextFile("tools/smoke-verify/run.ps1");
    requireContains(runner, "$invokeOutput = @(Invoke-SmokeTest");
    requireContains(runner, "Properties.Match('Passed').Count -gt 0");
    requireContains(runner, "Properties.Match('AssertionsTotal').Count -gt 0");
    requireContains(runner, "$resultCandidates.Count -ne 1");
    requireContains(runner, "$results += $resultCandidates[0]");
}

TEST_CASE("multi-process smoke isolates process installs and assertions",
    "[smoke][tooling][network][v009][b1031][static]")
{
    const std::string runner = readTextFile("tools/smoke-verify/run.ps1");
    const std::string systemSource = readTextFile("port/src/system.c");

    requireContains(runner, "separate_process_installs");
    requireContains(runner, "process install[{0}]: {1}");
    requireContains(runner, "Initialize-MultiProcessSmokeInstall -Definition $def");
    requireContains(runner, "Initialize-MultiProcessSmokeInstall -Definition $plan.Definition");
    requireContains(runner, "$processExe = Join-Path $processInstallInfo.InstallDir $exeLeaf");
    requireContains(runner, "$psi.FileName         = $processExe");
    requireContains(runner, "pd-identity.dat");
    REQUIRE(runner.find("$psi.FileName         = $exe\n") == std::string::npos);
    requireContains(runner, "\"--basedir\", [string]$processInstallInfo.InstallDir");
    requireContains(runner, "\"--savedir\", [string]$processInstallInfo.InstallDir");
    requireContains(runner, "\"--moddir\", [string]$processModsDir");
    requireContains(runner, "\"--debug-home-path\", [string]$processInstallInfo.InstallDir");
    requireContains(runner, "$allArgs = @(\"--smoke\", $processSmokePath) + $crashArgs + $isolationArgs + $pBootArgs");
    requireContains(runner, "Properties.Match('smoke_path').Count -gt 0");
    requireContains(runner, "Properties.Match('snapshot_log_file').Count -gt 0");
    requireContains(runner, "reset sequential log before launch");
    requireContains(runner, "Remove-Item -LiteralPath $logPath -Force -ErrorAction Stop");
    requireContains(runner, "Properties.Match('snapshot_exit_timeout_seconds').Count -gt 0");
    requireContains(runner, "Copy-Item -LiteralPath $logPath -Destination $snapshotPath -Force");
    requireContains(runner, "$psi.WorkingDirectory = $processInstallInfo.InstallDir");
    requireContains(runner, "Invoke-SmokeAssertions -LogPath $entry.LogPath");
    requireContains(runner, "retained process install for debugging");
    requireContains(runner, "cleaned process install:");

    requireContains(systemSource, "if (sysArgCheck(\"--smoke\"))");
    requireContains(systemSource, "sysArgGetString(\"--debug-home-path\")");
    requireContains(systemSource, "strncpy(outPath, debugHome, outLen - 1)");

    const std::string modmgr = readTextFile("port/src/modmgr.c");
    requireContains(modmgr, "const char *explicitModsDir = fsGetModDir();");
    requireContains(modmgr, "if (explicitModsDir && explicitModsDir[0]) {");
    requireContains(modmgr, "candidateBufs[1][0] = '\\0';");
    requireContains(modmgr, "modmgr: could not open explicit mod directory");
}

TEST_CASE("reconnect smoke drives one real timeout and one ordinary-client retry",
	"[smoke][network][reconnect][b1064][b1099][b1100][static]")
{
	const std::string schema = readTextFile("port/src/smoke_fixture_schema.c");
	const std::string harness = readTextFile("port/src/smoke_harness.c");
	const std::string runner = readTextFile("tools/smoke-verify/run.ps1");
	const std::string parent = readTextFile(
		"tools/smoke-verify/tests/network_reconnect_smoke.json");
	const std::string host = readTextFile(
		"tools/smoke-verify/fixtures/network_reconnect_host.json");
	const std::string client = readTextFile(
		"tools/smoke-verify/fixtures/network_reconnect_client.json");

	requireContains(schema, "network_timeout_client");
	requireContains(schema, "network_reconnect");
	requireContains(schema, "SMOKE_FIXTURE_FIELD_CLIENT_ID");
	requireContains(harness, "netServerKick(target, DISCONNECT_TIMEOUT)");
	requireContains(harness, "!target->stage_ready");
	requireContains(harness, "stage_ready=1 at_ms=%d production_path=1");
	requireContains(harness, "const s32 rc = netClientReconnect()");
	requireContains(harness,
		"facts.network_listen_ready = g_NetMode == NETMODE_SERVER");
	requireContains(harness, "&& netGetHost() != NULL;");
	requireContains(harness, "facts.network_reconnect_available = netClientReconnectAvailable()");
	requireContains(runner, "throw (\"Failed to parse {0}: {1}\"");
	requireContains(runner, "throw \"No tests matched the selection criteria.\"");
	requireContains(parent, "terminal_absent=[0-9]+");
	requireContains(parent, "first_terminal_absent=[0-9]+");
	requireContains(parent, "separate_process_installs");
	requireContains(parent, "network_reconnect_host.json");
	requireContains(parent, "network_reconnect_client.json");
	requireContains(parent, "\"--debug-spawn-weapon\", \"base:cyclone\"");
	requireContains(parent,
		"BOOT: --debug-spawn-weapon 'base:cyclone' \\\\(SPECIFIC\\\\)");
	requireContains(parent, "status=waiting exact_settings=1");
	requireContains(parent, "NET.DISCONNECT.INTENT latch client=1 reason=5 retryable=1");
	requireContains(parent, "server_intent=1 retryable=1");
	requireContains(parent, "NET.DISCONNECT.INTENT duplicate client=1");
	requireContains(parent, "status=stage_queued stage_bytes=");
	requireContains(parent, "commit=pending_post_load");
	requireContains(parent,
		"CUTSCENE.AUTHORITY: stage-start presentation previous_tickmode=6 next_tickmode=0 stale_cutscene_retired=1");
	requireContains(parent, "NET.RECONNECT.WORLD begin");
	requireContains(parent,
		"removed=[0-9]+ detached=[0-9]+ first_dynamic=[1-9][0-9]*");
	requireContains(parent, "NET.RECONNECT.WORLD end");
	requireContains(parent, "NET.RECONNECT.INVENTORY client=0");
	requireContains(parent, "NET.RECONNECT.INVENTORY client=1");
	requireContains(parent,
		"dynamic_prop=[0-9]+ terminal_absent=[0-9]+ first_terminal_absent=[0-9]+ exact_set=1");
	requireContains(parent, "NET.RECONNECT.RESYNC client=1");
	requireContains(parent, "NET.RECONNECT.CLIENT commit=accepted client=1");
	requireContains(parent, "cookie_preserved=1");
	requireContains(parent, "reserved-restore-ordered-resync-and-authoritative-fire");
	requireContains(parent, "NET.RECONNECT.GAMEPLAY client=1");
	requireContains(parent, "NET: .* \\\\(1\\\\) reconnected player=1");
	requireContains(parent, "restored-client-gameplay-consumes-fire");
	requireContains(parent, "COMBAT: SHOT_FIRED .*weapon=11(?: |$)");
	requireContains(parent, "scripted-exit-retires-reconnect-credential");
	requireContains(parent,
		"NET.RECONNECT.TEARDOWN retryable=0 credential_retained=0");
	REQUIRE(parent.find("NET.RECONNECT.TEARDOWN .*credential_retained=0") ==
		std::string::npos);
	REQUIRE(parent.find("LOG.WPN.DIAG: fire-input pi=0 fire_held=1") ==
		std::string::npos);
	const size_t presentationBoundary = parent.find(
		"CUTSCENE.AUTHORITY: stage-start presentation previous_tickmode=6");
	const size_t worldBegin = parent.find("NET.RECONNECT.WORLD begin",
		presentationBoundary);
	REQUIRE(presentationBoundary != std::string::npos);
	REQUIRE(worldBegin != std::string::npos);
	REQUIRE(presentationBoundary < worldBegin);
	requireContains(host, "\"type\": \"network_timeout_client\"");
	requireContains(host, "\"client_id\": 1");
	requireContains(host, "\"timeout_seconds\": 400");
	const size_t listenReady = host.find(
		"\"condition\": \"network_listen_ready\"");
	const size_t stageLive = host.find(
		"\"condition\": \"network_stage_live\"");
	REQUIRE(listenReady != std::string::npos);
	REQUIRE(stageLive != std::string::npos);
	REQUIRE(listenReady < stageLive);
	requireContains(host, "\"at_ms\": 15000");
	requireContains(host, "\"at_ms\": 105000");
	requireContains(client, "\"condition\": \"network_reconnect_available\"");
	requireContains(client, "\"type\": \"network_reconnect\"");
}

TEST_CASE("temporary asset recovery smoke covers keep disable discard and restart",
    "[smoke][tooling][network][catalog][t-networking-009][b1044][static]")
{
    const std::string source = readTextFile("port/src/smoke_harness.c");
    const std::string runner = readTextFile("tools/smoke-verify/run.ps1");
    const std::string fixture = readTextFile(
        "tools/smoke-verify/lib/Temporary-Recovery-Fixtures.ps1");
    const std::string scenario = readTextFile(
        "tools/smoke-verify/tests/temporary_asset_crash_recovery_smoke.json");

    requireContains(source, "SMOKE_EVENT_UNCLEAN_EXIT");
    requireContains(source, "SMOKE_EVENT_CATALOG_RECOVERY_PROBE");
    requireContains(source, "_Exit(0);");
    requireContains(source, "catalogLoadTypedAsset(type, asset_id)");
    requireContains(source, "entry->source.primary.provider == fileProvider()");
    requireContains(source, "assetRuntimeFindByTypeAndId(entry->type, asset_id)");
    requireContains(runner, "New-TemporaryRecoveryFixtures");
    requireContains(fixture, "tri_weapon_recovery.pdca");
    requireContains(fixture, "|recovery_tri_weapon|weapon|1");
    requireContains(scenario, "\"name\": \"seed-keep\"");
    requireContains(scenario, "\"name\": \"keep\"");
    requireContains(scenario, "\"name\": \"disable\"");
    requireContains(scenario, "\"name\": \"restart-disabled\"");
    requireContains(scenario, "\"name\": \"discard\"");
    requireContains(scenario, "\"name\": \"final-restart\"");
    requireContains(scenario, "DISTRIB.RECOVERY.KEEP.PASS");
    requireContains(scenario, "DISTRIB.RECOVERY.DISABLE.PASS");
    requireContains(scenario,
        "DISTRIB.RECOVERY.DISCARD.PASS: catalog retired temp_root=retired");
}

TEST_CASE("Needler real peer smoke starts client without the host fixture",
    "[smoke][network][manifest][distribution][v009][b1031][static]")
{
    const std::string scenario = readTextFile(
        "tools/smoke-verify/tests/needler_effect_real_peer_distribution_smoke.json");

    requireContains(scenario, "\"separate_process_installs\": true");
    requireContains(scenario, "\"name\": \"host\"");
    requireContains(scenario, "\"name\": \"client\"");
    requireContains(scenario, "needler_enabled.json");
    requireContains(scenario, "no_mods_enabled.json");
    requireContains(scenario, "mods/installed/needler.pdmod");
    requireContains(scenario, "NET: ready gate: client \\\\d+ NEED_ASSETS");
    requireContains(scenario, "DISTRIB: recv begin 'mod_needler:needler'");
    requireContains(scenario, "DISTRIB\\\\.CATALOG\\\\.ADMISSION: id=mod_needler:needler");
    requireContains(scenario, "MATCH: client stage start received");
    requireContains(scenario, "EFFECT\\\\.GAMEPLAY\\\\.AUDIT: committed asset=mod_needler:pink_burst_effect");
    requireContains(scenario, "EFFECT\\\\.PRESENTATION\\\\.RENDER\\\\.AUDIT:");
}

TEST_CASE("smoke can prove overlapping weapon owners through production lifecycle",
    "[catalog][weapon][effect][smoke][v009][owner][static]")
{
    const std::string source = readTextFile("port/src/smoke_harness.c");
    const std::string scenario = readTextFile(
        "tools/smoke-verify/tests/needler_effect_overlapping_owner_smoke.json");

    requireContains(source, "SMOKE_EVENT_CATALOG_WEAPON_ACQUIRE");
    requireContains(source, "SMOKE_EVENT_CATALOG_WEAPON_RELEASE");
    requireContains(source, "!strcmp(type_str, \"catalog_weapon_acquire\")");
    requireContains(source, "catalogLoadTypedAsset(ASSET_WEAPON, ev->path)");
    requireContains(source, "catalogReleaseTypedAsset(ASSET_WEAPON, ev->path)");
    requireContains(source, "SMOKE: catalog_weapon_owner op=%s id='%s' result=%d");
    requireContains(scenario, "\"type\": \"catalog_weapon_acquire\"");
    requireContains(scenario, "\"type\": \"catalog_weapon_release\"");
    requireContains(scenario, "modmgr: applying changes");
    requireContains(scenario, "PopStyleColor\\\\(\\\\) too many times");
    requireContains(scenario, "Missing PopStyleColor");
    requireContains(scenario, "ref=2->1 \\\\(retained\\\\)");
    requireContains(scenario, "ref=1->0 \\\\(freed\\\\)");
}
