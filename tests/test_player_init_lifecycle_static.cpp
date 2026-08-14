/*
 * Static production-path guards for T-ENGINE-004 player/match lifecycle
 * boundaries that are too coupled to link into the globals-free test runner.
 */

#include "catch.hpp"

#include <fstream>
#include <sstream>
#include <string>

namespace {

std::string read_player_init_source(const char *path)
{
	std::ifstream in(path, std::ios::in | std::ios::binary);
	REQUIRE(in.good());
	std::ostringstream out;
	out << in.rdbuf();
	return out.str();
}

std::string player_init_function(const std::string &text, const char *signature)
{
	const size_t begin = text.find(signature);
	REQUIRE(begin != std::string::npos);
	const size_t brace = text.find('{', begin);
	REQUIRE(brace != std::string::npos);

	size_t depth = 0;
	for (size_t pos = brace; pos < text.size(); ++pos) {
		if (text[pos] == '{') {
			++depth;
		} else if (text[pos] == '}') {
			REQUIRE(depth > 0);
			if (--depth == 0) {
				return text.substr(begin, pos - begin + 1);
			}
		}
	}

	FAIL("unterminated function block");
	return {};
}

} /* namespace */

TEST_CASE("B-1066 gun reset matches fresh-player weapon defaults",
          "[player-init][reset][static][B-1066]")
{
	const std::string source = read_player_init_source("src/game/bondgunreset.c");
	const std::string reset = player_init_function(source, "void bgunReset(void)");

	REQUIRE(reset.find("gunctrl.weaponnum = WEAPON_NONE;") != std::string::npos);
	REQUIRE(reset.find("gunctrl.prevweaponnum = -1;") != std::string::npos);
	REQUIRE(reset.find("gunctrl.prevwasdualwielding = false;") != std::string::npos);
	REQUIRE(reset.find("gunctrl.wantammo = false;") != std::string::npos);
	REQUIRE(reset.find("gunctrl.passivemode = false;") != std::string::npos);
	REQUIRE(reset.find("memset(&g_Vars.currentplayer->gunctrl") == std::string::npos);
}

TEST_CASE("B-1066 stage reset consumes stale jump input",
          "[player-init][reset][static][B-1066]")
{
	const std::string source = read_player_init_source("src/game/playerreset.c");
	const std::string reset = player_init_function(source, "void playerReset(void)");

	REQUIRE(reset.find("currentplayer->wantsjump = false;") != std::string::npos);
	REQUIRE(reset.find("currentplayer->jumpconsumed = true;") != std::string::npos);
	REQUIRE(reset.find("memset(g_Vars.currentplayer") == std::string::npos);
}

TEST_CASE("B-1072 player defaults clear only the gun-function field",
          "[player-init][defaults][bounds][static][b1072]")
{
	const std::string source = read_player_init_source(
		"src/game/mplayer/mplayer.c");
	const std::string defaults = player_init_function(
		source, "void mpPlayerSetDefaults");

	REQUIRE(defaults.find(
		"memset(g_PlayerConfigsArray[playernum].gunfuncs, 0,") !=
		std::string::npos);
	REQUIRE(defaults.find(
		"sizeof(g_PlayerConfigsArray[playernum].gunfuncs)") !=
		std::string::npos);
	REQUIRE(defaults.find("ARRAYCOUNT(g_PlayerConfigsArray)") ==
		std::string::npos);
}

TEST_CASE("B-1074 KillMaster excludes each active participant's own deaths",
          "[player-init][combat-sim][awards][static][b1074]")
{
	const std::string source = read_player_init_source(
		"src/game/mplayer/mplayer.c");
	const std::string awards = player_init_function(
		source, "void mpCalculateAwards(void)");

	REQUIRE(awards.find("if (k != j)") != std::string::npos);
	REQUIRE(awards.find("if (i != j)") == std::string::npos);
	REQUIRE(awards.find("mostkillsplayer < MAX_PLAYERS") !=
		std::string::npos);
	REQUIRE(awards.find("leastdeathsplayer < MAX_PLAYERS") !=
		std::string::npos);
	REQUIRE(awards.find("mostkillsplayer < 4") == std::string::npos);
	REQUIRE(awards.find("leastdeathsplayer < 4") == std::string::npos);
	REQUIRE(awards.find("combatSimVerifyRecordAwardParticipant") !=
		std::string::npos);
	REQUIRE(awards.find("combatSimVerifyRecordKillMaster") !=
		std::string::npos);
}

TEST_CASE("Combat Simulator verification follows production start end and return boundaries",
		"[player-init][combat-sim][cycle][static][b1065][b1066][b1074][b1077][b1081]")
{
	const std::string main = read_player_init_source("port/src/main.c");
	const std::string pdmain = read_player_init_source("port/src/pdmain.c");
	const std::string room = read_player_init_source(
		"port/fast3d/pdgui_menu_room.cpp");
	const std::string endscreen = read_player_init_source(
		"port/fast3d/pdgui_menu_endscreen.cpp");
	const std::string mplayer = read_player_init_source(
		"src/game/mplayer/mplayer.c");
	const std::string verifier = read_player_init_source(
		"port/src/combat_sim_verify.c");
	const std::string actionmap = read_player_init_source(
		"port/src/actionmap.cpp");
	const std::string smoke = read_player_init_source(
		"tools/smoke-verify/tests/combat_sim_release_cycle.json");

	const std::string direct = player_init_function(
		main, "s32 bootLaunchMpMatchTick(void)");
	const std::string place_bot = player_init_function(
		main, "s32 bootDebugPlaceBotNearPlayerPreRenderTick(void)");
	const std::string options = player_init_function(
		main, "static void bootApplyDebugMpOptions(void)");
	const std::string room_start = player_init_function(
		room, "static s32 roomGraphStartMatch");
	const std::string room_auto = player_init_function(
		room, "extern \"C\" void pdguiRoomScreenRender");
	const std::string continue_match = player_init_function(
		endscreen, "static s32 endscreenGraphMpContinue");
	const std::string end_match = player_init_function(
		mplayer, "void mpEndMatch(void)");

	REQUIRE(options.find("matchConfigReplaceUserOptions") !=
		std::string::npos);
	REQUIRE(options.find("g_MpSetup.options = matchConfigGetUserOptions()") !=
		std::string::npos);
	REQUIRE(direct.find("combatSimVerifyOnMatchStart(\"direct\")") !=
		std::string::npos);
	REQUIRE(room_start.find("combatSimVerifyOnMatchStart(\"room\")") !=
		std::string::npos);
	REQUIRE(room_auto.find("combatSimVerifyConsumeRoomAutoStart") !=
		std::string::npos);
	REQUIRE(continue_match.find("pdguiSoloRoomReturn();") !=
		std::string::npos);
	REQUIRE(continue_match.find("combatSimVerifyOnRoomReturn();") !=
		std::string::npos);
	const size_t prepare_awards = end_match.find(
		"combatSimVerifyPrepareAwards();");
	const size_t calculate_awards = end_match.find("mpCalculateAwards();");
	const size_t verify_end = end_match.find("combatSimVerifyOnMatchEnd();");
	REQUIRE(prepare_awards != std::string::npos);
	REQUIRE(calculate_awards != std::string::npos);
	REQUIRE(verify_end != std::string::npos);
	REQUIRE(prepare_awards < calculate_awards);
	REQUIRE(calculate_awards < verify_end);
	REQUIRE(pdmain.find("combatSimVerifyTick();") != std::string::npos);
	REQUIRE(main.find("--debug-place-bot-near-player-hold-sec") !=
		std::string::npos);
	REQUIRE(place_bot.find("g_Vars.tickmode != TICKMODE_NORMAL") !=
		std::string::npos);
	REQUIRE(place_bot.find(
		"k_BootDebugPlaceBotNearPlayerStableFrames") != std::string::npos);
	REQUIRE(place_bot.find("cdFindGroundInfoAtCyl(&candidate, bot->radius, rooms") !=
		std::string::npos);
	REQUIRE(place_bot.find("meshFindFloor(&candidate, bot->radius, &meshnormaly)") !=
		std::string::npos);
	REQUIRE(place_bot.find("playerground = g_Vars.currentplayer->vv_ground") !=
		std::string::npos);
	REQUIRE(place_bot.find("fabsf(selectedground - playerground) <= 80.0f") !=
		std::string::npos);
	REQUIRE(place_bot.find("target.y = selectedground") !=
		std::string::npos);
	REQUIRE(place_bot.find("selectedsource = \"mesh\"") != std::string::npos);
	REQUIRE(place_bot.find("candidatefloorroom = (RoomNum)-1") !=
		std::string::npos);
	REQUIRE(place_bot.find("chrRelocateToFloorWithCachedGround(bot, &target") !=
		std::string::npos);
	REQUIRE(place_bot.find("playerprop->pos.y - playerground, rooms") !=
		std::string::npos);
	REQUIRE(place_bot.find("chrSetPosWithCachedGround(bot, &target, rooms") ==
		std::string::npos);
	REQUIRE(place_bot.find("chrSetPos(bot, &target, rooms") ==
		std::string::npos);
	REQUIRE(place_bot.find("chrMoveToPos(bot, &target, rooms") ==
		std::string::npos);
	REQUIRE(place_bot.find("PROPFLAG_ONTHISSCREENTHISTICK") ==
		std::string::npos);
	REQUIRE(place_bot.find("propActivateThisFrame(bot->prop)") ==
		std::string::npos);
	REQUIRE(place_bot.find("SDL_GetTicks()") != std::string::npos);
	REQUIRE(verifier.find("g_Vars.stagenum != g_MpSetup.stagenum") !=
		std::string::npos);
	REQUIRE(verifier.find("combatSimVerifyPrepareAwards(void)") !=
		std::string::npos);
	REQUIRE(smoke.find("0:0=4,0:8=1,8:0=2") != std::string::npos);
	REQUIRE(smoke.find("0x00200001") != std::string::npos);
	REQUIRE(smoke.find("--debug-place-bot-near-player-hold-sec") !=
		std::string::npos);
	const size_t hold_arg = smoke.find(
		"--debug-place-bot-near-player-hold-sec");
	const size_t hold_value = smoke.find("\"15\"", hold_arg);
	const size_t first_post_hold_fire = smoke.find(
		"\"at_ms\": 70000, \"type\": \"action\", \"name\": \"ACTION_FIRE_PRIMARY\"");
	REQUIRE(hold_value != std::string::npos);
	REQUIRE(first_post_hold_fire != std::string::npos);
	REQUIRE(hold_arg < hold_value);
	REQUIRE(hold_value < first_post_hold_fire);
	REQUIRE(smoke.find(
		"\"at_ms\": 50000, \"type\": \"action\", \"name\": \"ACTION_FIRE_PRIMARY\"") ==
		std::string::npos);
	REQUIRE(smoke.find(
		"\"at_ms\": 60000, \"type\": \"action\", \"name\": \"ACTION_FIRE_PRIMARY\"") ==
		std::string::npos);
	REQUIRE(smoke.find("\"name\": \"ACTION_USE\"") !=
		std::string::npos);
	REQUIRE(actionmap.find("SMOKE.ACTION.INJECT: player=%d action=%d down=%d") !=
		std::string::npos);
	REQUIRE(actionmap.find("held_before, pressed_before, released_before") !=
		std::string::npos);
	REQUIRE(endscreen.find("ENDSCREEN.DIAG: input action_bar_visible=%d") !=
		std::string::npos);
	REQUIRE(endscreen.find("actionPressed(0, ACTION_MENU_ACCEPT)") !=
		std::string::npos);
	REQUIRE(smoke.find("\"type\": \"mouse_move\"") == std::string::npos);
	REQUIRE(smoke.find("MATCH\\\\.CYCLE: start ordinal=") !=
		std::string::npos);
	REQUIRE(smoke.find("MENU: watchdog") != std::string::npos);
	REQUIRE(smoke.find("MODEL\\\\.GDL\\\\.REJECT") !=
		std::string::npos);
}

TEST_CASE("floor-domain actor warp latches root height across animation loops",
		"[player-init][actor][teleport][static][b1077]")
{
	const std::string action_header = read_player_init_source("src/include/game/chraction.h");
	const std::string chraction_source = read_player_init_source("src/game/chraction.c");
	const std::string model_header = read_player_init_source("src/include/lib/model.h");
	const std::string model_source = read_player_init_source("src/lib/model.c");
	const std::string types_header = read_player_init_source("src/include/types.h");
	const std::string relocate = player_init_function(
		chraction_source, "bool chrRelocateToFloorWithCachedGround");
	const std::string set_height = player_init_function(
		model_source, "bool modelSetChrRootHeight");
	const std::string set_animation = player_init_function(
		model_source, "void modelSetAnimation2");
	const std::string advance_animation = player_init_function(
		model_source, "void modelSetAnimFrame2WithChrStuff");
	const std::string model_init = player_init_function(
		model_source, "void modelInit(");
	const std::string cached_ground = player_init_function(
		chraction_source, "void chrSetPosWithCachedGround");
	const std::string move_to_pos = player_init_function(
		chraction_source, "bool chrMoveToPos");

	REQUIRE(action_header.find(
		"bool chrRelocateToFloorWithCachedGround(struct chrdata *chr") !=
		std::string::npos);
	REQUIRE(action_header.find(
		"const struct coord *floorpos, f32 rootheight") !=
		std::string::npos);
	REQUIRE(model_header.find("bool modelSetChrRootHeight(struct model *model") !=
		std::string::npos);
	REQUIRE(types_header.find("f32 chrrootheightbias;") !=
		std::string::npos);
	REQUIRE(types_header.find("u8 chrrootheightenabled;") !=
		std::string::npos);
	REQUIRE(set_height.find("modelReadAuthoredChrRootHeight(model, &authoredheight)") !=
		std::string::npos);
	REQUIRE(set_height.find("ANIMFLAG_ABSOLUTETRANSLATION") !=
		std::string::npos);
	REQUIRE(set_height.find("bias = rootheight - authoredheight;") !=
		std::string::npos);
	REQUIRE(set_height.find("rwdata->unk34.y = rootheight;") != std::string::npos);
	REQUIRE(set_height.find("rwdata->unk24.y = rootheight;") != std::string::npos);
	REQUIRE(set_height.find("rwdata->unk4c.y = rootheight;") != std::string::npos);
	REQUIRE(set_height.find("rwdata->unk40.y = rootheight;") != std::string::npos);
	REQUIRE(set_height.find("*model->anim =") == std::string::npos);
	REQUIRE(set_height.find("modelSetAnimation(") == std::string::npos);
	REQUIRE(set_animation.find("modelApplyChrRootHeightBias(model, anim->animnum") !=
		std::string::npos);
	REQUIRE(advance_animation.find("modelApplyChrRootHeightBias(model, anim->animnum") !=
		std::string::npos);
	REQUIRE(advance_animation.find("modelApplyChrRootHeightBias(model, anim->animnum2") !=
		std::string::npos);
	REQUIRE(model_init.find("model->chrrootheightenabled = false;") !=
		std::string::npos);
	REQUIRE(relocate.find("modelSetChrRootHeight(chr->model, rootheight)") !=
		std::string::npos);
	REQUIRE(relocate.find("rootpos.y = floorpos->y + rootheight;") !=
		std::string::npos);
	REQUIRE(relocate.find("modelSetAnimation(") ==
		std::string::npos);
	REQUIRE(relocate.find(
		"chrSetPosWithCachedGround(chr, &rootpos, rooms, theta, floorpos->y") !=
		std::string::npos);
	REQUIRE(cached_ground.find("modelSetRootPosition(chr->model, pos)") !=
		std::string::npos);
	REQUIRE(move_to_pos.find("modelSetRootPosition(chr->model, &pos2)") !=
		std::string::npos);
	REQUIRE(cached_ground.find("chrRelocateToFloorWithCachedGround") ==
		std::string::npos);
	REQUIRE(move_to_pos.find("chrRelocateToFloorWithCachedGround") ==
		std::string::npos);
}

TEST_CASE("B-1065 option overlay uses shared teardown and replacement boundaries",
          "[player-init][options][static][B-1065]")
{
	const std::string pdmain = read_player_init_source("port/src/pdmain.c");
	const std::string net = read_player_init_source("port/src/net/net.c");
	const std::string netmsg = read_player_init_source("port/src/net/netmsg.c");
	const std::string room = read_player_init_source("port/fast3d/pdgui_menu_room.cpp");

	const std::string end_stage = player_init_function(pdmain, "void mainEndStage(void)");
	const std::string disconnect_entry = player_init_function(
		net, "s32 netDisconnect(void)");
	const std::string disconnect = player_init_function(
		net, "static s32 netDisconnectWithIntent");
	const std::string room_settings = player_init_function(
		netmsg, "u32 netmsgSvcRoomSettingsRead");
	const std::string leave_room = player_init_function(room, "static s32 roomGraphLeaveRoom");

	REQUIRE(end_stage.find("matchConfigRestoreUserOptions(\"mainEndStage\")") != std::string::npos);
	REQUIRE(disconnect_entry.find("netDisconnectWithIntent(false)") !=
		std::string::npos);
	REQUIRE(disconnect.find("matchConfigRestoreUserOptions(\"netDisconnect\")") != std::string::npos);
	REQUIRE(room_settings.find("matchConfigReplaceUserOptions(options, \"SVC_ROOM_SETTINGS\")")
	        != std::string::npos);
	REQUIRE(leave_room.find("matchConfigRestoreUserOptions(\"room leave\")") != std::string::npos);

	REQUIRE(room.find("matchConfigGetUserOptions()") != std::string::npos);
	REQUIRE(room.find("matchConfigSetUserOption(flag") != std::string::npos);
}

TEST_CASE("solo room closes only after match start commits",
          "[player-init][match-start][static][B-1064]")
{
	const std::string room = read_player_init_source("port/fast3d/pdgui_menu_room.cpp");
	const std::string start = player_init_function(room, "static s32 roomGraphStartMatch");
	const size_t guarded_start = start.find("if (matchStart() != 0)");
	const size_t close = start.find("pdguiSoloRoomClose();", guarded_start);

	REQUIRE(guarded_start != std::string::npos);
	REQUIRE(close != std::string::npos);
	REQUIRE(guarded_start < close);
}

TEST_CASE("B-1069 network launch consumes the frozen typed match",
          "[player-init][network][transaction][static][B-1069]")
{
	const std::string mplayer = read_player_init_source(
		"src/game/mplayer/mplayer.c");
	const std::string netmsg = read_player_init_source("port/src/net/netmsg.c");
	const std::string matchsetup = read_player_init_source(
		"port/src/net/matchsetup.c");
	const std::string start = player_init_function(mplayer, "void mpStartMatch");
	const std::string lobby_write = player_init_function(
		netmsg, "static bool netLobbyStartWritePrepare");
	const std::string lobby_read = player_init_function(
		netmsg, "u32 netmsgClcLobbyStartRead");
	const std::string stage_write = player_init_function(
		netmsg, "static bool netStageStartWritePrepare");
	const std::string stage_client = player_init_function(
		netmsg, "static bool netStageStartWriteAppendClient");
	const std::string set_slot = player_init_function(
		matchsetup, "s32 matchConfigSetWeaponSlotId");

	const size_t network_flag = start.find("const bool network_prepared");
	const size_t offline_gate = start.find("if (!network_prepared)", network_flag);
	const size_t immutable_branch = start.find("} else {", offline_gate);
	REQUIRE(network_flag != std::string::npos);
	REQUIRE(offline_gate != std::string::npos);
	REQUIRE(immutable_branch != std::string::npos);
	REQUIRE(offline_gate < immutable_branch);
	const std::string offline_setup = start.substr(
		offline_gate, immutable_branch - offline_gate);
	REQUIRE(offline_setup.find("matchConfigSelectWeaponSet") != std::string::npos);
	REQUIRE(offline_setup.find("mpConfigureQuickTeamSimulants") != std::string::npos);
	REQUIRE(offline_setup.find("challengeIsFeatureUnlocked") != std::string::npos);
	REQUIRE(offline_setup.find("mpResolveMatchStage") != std::string::npos);
	REQUIRE(start.substr(immutable_branch).find("mpApplyWeaponSet") ==
		std::string::npos);
	REQUIRE(start.substr(immutable_branch).find("mpConfigureQuickTeamSimulants") ==
		std::string::npos);
	REQUIRE(start.substr(immutable_branch).find("challengeIsFeatureUnlocked") ==
		std::string::npos);
	REQUIRE(start.substr(immutable_branch).find("mpResolveMatchStage") ==
		std::string::npos);
	REQUIRE(start.find("mpRemoveParticipant") == std::string::npos);
	REQUIRE(start.find("mpAddParticipantAt") == std::string::npos);

	REQUIRE(lobby_write.find("MPOPTION_AUTORANDOMWEAPON_START") !=
		std::string::npos);
	REQUIRE(lobby_write.find("memcpy(plan->weapons, preset_weapons") !=
		std::string::npos);
	REQUIRE(lobby_write.find("plan->weapon_ids[wi]") != std::string::npos);
	REQUIRE(lobby_write.find("g_RngSeed = seed_0;") != std::string::npos);
	REQUIRE(lobby_write.find("g_Rng2Seed = seed_1;") != std::string::npos);
	REQUIRE(lobby_read.find("already resolved exact") != std::string::npos);
	REQUIRE(lobby_read.find("memcpy(prepared_weapons, plan.weapons") !=
		std::string::npos);

	REQUIRE(stage_write.find("netStageStartWriteAppendClient") !=
		std::string::npos);
	REQUIRE(stage_client.find("handicap == 0") != std::string::npos);
	REQUIRE(stage_client.find("name_length >= sizeof(settings->name)") !=
		std::string::npos);
	REQUIRE(stage_client.find(
		"name_length >= sizeof(plan->clients[0].name)") !=
		std::string::npos);
	REQUIRE(stage_write.find("bot_name_length >= sizeof(bot->base.name)") !=
		std::string::npos);

	REQUIRE(set_slot.find("g_MatchConfig.weaponSetIndex = -1;") !=
		std::string::npos);
	REQUIRE(set_slot.find("mpCommitPreparedWeaponSet(WEAPONSET_CUSTOM") !=
		std::string::npos);
}
