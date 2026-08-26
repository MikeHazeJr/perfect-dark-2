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
	size_t begin = text.find(signature);
	size_t brace = std::string::npos;

	while (begin != std::string::npos) {
		brace = text.find('{', begin);
		const size_t semicolon = text.find(';', begin);
		if (brace != std::string::npos
				&& (semicolon == std::string::npos || brace < semicolon)) {
			break;
		}
		begin = text.find(signature,
			semicolon == std::string::npos ? begin + 1 : semicolon + 1);
	}

	REQUIRE(begin != std::string::npos);
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

size_t count_substring(const std::string &text, const std::string &needle)
{
	size_t count = 0;
	size_t offset = 0;

	while ((offset = text.find(needle, offset)) != std::string::npos) {
		count++;
		offset += needle.size();
	}

	return count;
}

} /* namespace */

TEST_CASE("B-1101 frames are bounded and B-1102 rejection keeps bind pose safe",
		"[player-init][anim][static][B-1101][B-1102]")
{
	const std::string modelasm = read_player_init_source("src/lib/modelasm_c.c");
	const std::string anim = read_player_init_source("src/lib/anim.c");
	const std::string bounded_bits = read_player_init_source("src/lib/anim_bits.c");
	const std::string matrix_core = player_init_function(modelasm,
		"static bool modelasmBuildMatrices");
	const std::string decode_failure = player_init_function(modelasm,
		"static bool modelasmHandleFrameDecodeFailure");

	REQUIRE(modelasm.find("t3ptr8 - t6ptr8") == std::string::npos);
	REQUIRE(modelasm.find("while (v1 > gp)") == std::string::npos);
	REQUIRE(modelasm.find("g_Anims[animnum].bytesperframe") !=
		std::string::npos);
	REQUIRE(modelasm.find("s_ModelasmFrameByteLen = bytelen;") !=
		std::string::npos);
	REQUIRE(modelasm.find("animReadBitsBounded") != std::string::npos);
	REQUIRE(modelasm.find("MODELASM_FRAME_DECODE_PAYLOAD_TRUNCATED") !=
		std::string::npos);
	REQUIRE(modelasm.find("MODELASM_FRAME_DECODE_GENERIC_REQUIRED") !=
		std::string::npos);
	REQUIRE(modelasm.find("MODELASM_FRAME_DECODE_INVALID_FLAGS") !=
		std::string::npos);
	REQUIRE(modelasm.find("MODELASM_FRAME_DECODE_METADATA_INVALID") !=
		std::string::npos);
	REQUIRE(modelasm.find("MODELASM_FRAME_DECODE_FIELD_WIDTH_INVALID") !=
		std::string::npos);
	REQUIRE(modelasm.find("MODELASM_FRAME_DECODE_PART_CAPACITY_EXCEEDED") !=
		std::string::npos);
	REQUIRE(modelasm.find("MODELASM_FRAME_DECODE_LAYOUT_MISMATCH") !=
		std::string::npos);
	REQUIRE(modelasm.find("MODELASM_FRAME_PART_CAPACITY") !=
		std::string::npos);
	REQUIRE(modelasm.find("modelasmBeginAnimationFrame") !=
		std::string::npos);
	REQUIRE(modelasm.find("modelasmFinishCurrentPart") !=
		std::string::npos);
	REQUIRE(modelasm.find("s_ModelasmExpectedPartHeaderEnd") !=
		std::string::npos);
	REQUIRE(modelasm.find("s_ModelasmExpectedPartFrameBitEnd") !=
		std::string::npos);
	REQUIRE(modelasm.find("modelasmRequireFramePartCapacity") !=
		std::string::npos);
	REQUIRE(modelasm.find("modelasmRequireFrameFieldWidth") !=
		std::string::npos);
	REQUIRE(modelasm.find("endoffset > bitlength") != std::string::npos);
	REQUIRE(bounded_bits.find("endoffset > bitlength") != std::string::npos);
	REQUIRE(bounded_bits.find("animFrameLocatePartBounded") !=
		std::string::npos);
	REQUIRE(anim.find("layout_result = animFrameLocatePartBounded") !=
		std::string::npos);
	REQUIRE(anim.find("animReadFrameField") != std::string::npos);
	REQUIRE(anim.find("s32 animReadBits(") == std::string::npos);
	REQUIRE(anim.find("animReadSignedShort") == std::string::npos);
	REQUIRE(anim.find("u16 animGetPosAngleAsInt") != std::string::npos);
	REQUIRE(anim.find("f32 animGetCameraValue") != std::string::npos);
	REQUIRE(anim.find("ANIM.FRAME.LAYOUT.REJECT") != std::string::npos);
	REQUIRE(modelasm.find("ANIM.FRAME.DECODE.REJECT") != std::string::npos);

	/* B-1102: the optimized matrix core takes animation ownership explicitly,
	 * treats the animnum-0 sentinel as bind pose, and owns a no-animation
	 * CHRINFO branch. Decode rejection can therefore preserve model->anim and
	 * re-enter the same complete traversal with an explicit NULL input. */
	REQUIRE(matrix_core.find("struct anim *anim") != std::string::npos);
	REQUIRE(matrix_core.find("anim != NULL && anim->animnum == 0") !=
		std::string::npos);
	REQUIRE(matrix_core.find("anim = NULL;") != std::string::npos);
	REQUIRE(matrix_core.find(
		"animscale = anim != NULL ? anim->animscale : 1.0f;") !=
		std::string::npos);
	REQUIRE(count_substring(matrix_core, "anim->animscale") == 1);
	REQUIRE(matrix_core.find("case MODELNODETYPE_CHRINFO:") !=
		std::string::npos);
	REQUIRE(matrix_core.find("if (anim && anim->animnum)") !=
		std::string::npos);
	REQUIRE(decode_failure.find(
		"return modelasmBuildMatrices(renderdata, model, NULL);") !=
		std::string::npos);
	REQUIRE(decode_failure.find("model->anim = NULL;") == std::string::npos);
	REQUIRE(decode_failure.find("modelUpdateMatrices(renderdata, model);") ==
		std::string::npos);
	REQUIRE(decode_failure.find("model->anim = anim;") == std::string::npos);
}

TEST_CASE("B-1101 private player candidates own explicit chr targets",
		"[player-init][target][static][B-1101]")
{
	const std::string chr_source = read_player_init_source("src/game/chr.c");
	const std::string reset_source = read_player_init_source("src/game/playerreset.c");
	const std::string legacy_init = player_init_function(
		chr_source, "void chrInit(struct prop *prop");
	const std::string explicit_init = player_init_function(
		chr_source, "bool chrInitWithTargetProp");
	const std::string set_target = player_init_function(
		chr_source, "bool chrSetTargetProp");
	const std::string get_target = player_init_function(
		chr_source, "struct prop *chrGetTargetProp");
	const std::string reset = player_init_function(
		reset_source, "enum player_reset_result playerReset");
	const std::string bind_eyespy = player_init_function(
		reset_source, "static bool playerBindEyespyTarget");

	REQUIRE(legacy_init.find("prop - g_Vars.props") == std::string::npos);
	REQUIRE(legacy_init.find("chrTargetPropIsPublishedPlayer") !=
		std::string::npos);
	REQUIRE(chr_source.find("cursor = g_Vars.activeprops") !=
		std::string::npos);
	REQUIRE(chr_source.find("visited < g_Vars.maxprops") !=
		std::string::npos);
	REQUIRE(explicit_init.find("chrSetTargetProp(chr, targetprop)") !=
		std::string::npos);
	REQUIRE(explicit_init.find("return targetbound;") != std::string::npos);
	REQUIRE(set_target.find("chr->target = -2;") != std::string::npos);
	REQUIRE(chr_source.find("offset % sizeof(*targetprop)") !=
		std::string::npos);
	REQUIRE(get_target.find("chr->target < 0") != std::string::npos);
	REQUIRE(get_target.find("return NULL;") != std::string::npos);
	REQUIRE(reset.find("currentplayer->prop = NULL;") != std::string::npos);
	REQUIRE(reset.find("currentplayer->eyespy = NULL;") != std::string::npos);
	REQUIRE(reset.find(
		"player_target_bound = chrInitWithTargetProp(playerprop, NULL, playerprop)") !=
		std::string::npos);
	REQUIRE(reset.find("if (!player_target_bound)") != std::string::npos);
	REQUIRE(reset.find("playerBindEyespyTarget(&eyespy_candidate, playerprop)") !=
		std::string::npos);
	REQUIRE(reset.find("PLAYER_RESET_TARGET_BIND_FAILED") !=
		std::string::npos);
	REQUIRE(bind_eyespy.find("chrSetTargetProp") != std::string::npos);
}

TEST_CASE("B-1066 gun reset matches fresh-player weapon defaults",
          "[player-init][reset][static][B-1066]")
{
	const std::string reset_source = read_player_init_source("src/game/bondgunreset.c");
	const std::string playerreset_source = read_player_init_source("src/game/playerreset.c");
	const std::string playermgr_source = read_player_init_source("src/game/playermgr.c");
	const std::string reset = player_init_function(reset_source, "void bgunReset(void)");
	const std::string defaults = player_init_function(playerreset_source,
		"void playerInitStageTransientDefaults");
	const std::string fresh = playermgr_source;

	REQUIRE(defaults.find("gunctrl.weaponnum = WEAPON_NONE;") != std::string::npos);
	REQUIRE(defaults.find("gunctrl.prevweaponnum = -1;") != std::string::npos);
	REQUIRE(defaults.find("gunctrl.prevwasdualwielding = false;") != std::string::npos);
	REQUIRE(defaults.find("gunctrl.wantammo = false;") != std::string::npos);
	REQUIRE(defaults.find("gunctrl.passivemode = false;") != std::string::npos);
	REQUIRE(defaults.find("wantsjump = false;") != std::string::npos);
	REQUIRE(defaults.find("jumpconsumed = true;") != std::string::npos);
	REQUIRE(defaults.find("client") == std::string::npos);
	REQUIRE(defaults.find("isremote") == std::string::npos);
	REQUIRE(defaults.find("ucmd") == std::string::npos);
	REQUIRE(defaults.find("memset(") == std::string::npos);
	REQUIRE(reset.find("playerInitStageTransientDefaults(g_Vars.currentplayer)") !=
		std::string::npos);
	REQUIRE(fresh.find("playerInitStageTransientDefaults(player)") !=
		std::string::npos);
}

TEST_CASE("B-1066 stage reset consumes stale jump input",
          "[player-init][reset][static][B-1066]")
{
	const std::string source = read_player_init_source("src/game/lv.c");
	const std::string reset = player_init_function(source, "bool lvReset(s32 stagenum)");
	const size_t gun_reset = reset.find("bgunReset();");
	const size_t player_reset = reset.find("playerReset()", gun_reset);
	const size_t player_spawn = reset.find("playerSpawn()", player_reset);

	REQUIRE(gun_reset != std::string::npos);
	REQUIRE(player_reset != std::string::npos);
	REQUIRE(player_spawn != std::string::npos);
	REQUIRE(gun_reset < player_reset);
	REQUIRE(player_reset < player_spawn);
}

TEST_CASE("B-1064 stage player creation preflights before prop publication",
		"[player-init][transaction][static][B-1064][T-ENGINE-004]")
{
	const std::string reset_source = read_player_init_source("src/game/playerreset.c");
	const std::string lv_source = read_player_init_source("src/game/lv.c");
	const std::string reset = player_init_function(reset_source,
		"enum player_reset_result playerReset(void)");
	const std::string lv = player_init_function(lv_source,
		"bool lvReset(s32 stagenum)");

	const size_t choose = reset.find("playerChooseBodyAndHead");
	const size_t preflight = reset.find("playerChrBodyPreflight", choose);
	const size_t prop_allocate = reset.find("playerprop = propAllocate()", preflight);
	const size_t prop_publish = reset.find("currentplayer->prop = playerprop", prop_allocate);

	REQUIRE(choose != std::string::npos);
	REQUIRE(preflight != std::string::npos);
	REQUIRE(prop_allocate != std::string::npos);
	REQUIRE(prop_publish != std::string::npos);
	REQUIRE(choose < preflight);
	REQUIRE(preflight < prop_allocate);
	REQUIRE(prop_allocate < prop_publish);
	REQUIRE(lv.find("player_result != PLAYER_RESET_OK") != std::string::npos);
	REQUIRE(lv.find("chrbody_result != PLAYER_CHRBODY_OK") != std::string::npos);
	REQUIRE(lv.find("aborting stage reset") != std::string::npos);
}

TEST_CASE("B-1064 chrbody late failures use one reversible publication boundary",
		"[player-init][chrbody][rollback][static][B-1064][T-ENGINE-004]")
{
	const std::string source = read_player_init_source("src/game/player.c");
	const std::string tick = player_init_function(source,
		"enum player_chrbody_result playerTickChrBody(void)");
	const size_t weapon_ready = tick.find("if (weaponmodeldef == NULL)");
	const size_t attach_chr = tick.find("chr0f020b14", weapon_ready);
	const size_t attach_weapon = tick.find("weaponCreateForChr", attach_chr);
	const size_t fireslot = tick.find("bgunAllocateFireslot", attach_weapon);
	const size_t publish_view = tick.find("currentplayer->vv_eyeheight = prepared_eyeheight", fireslot);
	const size_t publish_model = tick.find("currentplayer->model00d4 = prepared_model", fireslot);
	const size_t publish_flag = tick.find("currentplayer->haschrbody = true", publish_model);
	const size_t rollback = tick.find("player_init_rollback:", publish_flag);

	REQUIRE(weapon_ready != std::string::npos);
	REQUIRE(attach_chr != std::string::npos);
	REQUIRE(attach_weapon != std::string::npos);
	REQUIRE(fireslot != std::string::npos);
	REQUIRE(publish_view != std::string::npos);
	REQUIRE(publish_model != std::string::npos);
	REQUIRE(publish_flag != std::string::npos);
	REQUIRE(rollback != std::string::npos);
	REQUIRE(weapon_ready < attach_chr);
	REQUIRE(attach_chr < attach_weapon);
	REQUIRE(attach_weapon < fireslot);
	REQUIRE(fireslot < publish_view);
	REQUIRE(publish_view < publish_model);
	REQUIRE(fireslot < publish_model);
	REQUIRE(publish_model < publish_flag);
	REQUIRE(tick.find("weaponCreateForChr(chr", attach_chr) != std::string::npos);
	REQUIRE(tick.find("== NULL", attach_weapon) != std::string::npos);
	REQUIRE(tick.find("fireslots[0] < 0", fireslot) != std::string::npos);
	REQUIRE(tick.find("chrRemove(g_Vars.currentplayer->prop, !chr_was_preexisting)",
		rollback) != std::string::npos);
	REQUIRE(tick.find("currentplayer->prop->type = previous_prop_type", rollback) !=
		std::string::npos);
	REQUIRE(tick.find("currentplayer->prop->pos = previous_prop_pos", rollback) !=
		std::string::npos);
	REQUIRE(tick.find("roomsCopySafe(previous_prop_rooms", rollback) !=
		std::string::npos);
	REQUIRE(tick.find("propRegisterRooms(g_Vars.currentplayer->prop)", rollback) !=
		std::string::npos);
	REQUIRE(tick.find("currentplayer->prop->rooms[0] = -1", rollback) ==
		std::string::npos);
}

TEST_CASE("B-1064 runtime identity permits headless bodies only when complete",
		"[player-init][identity][static][B-1064][T-ENGINE-004]")
{
	const std::string source = read_player_init_source("port/src/player_identity.c");
	const std::string body_only = player_init_function(source,
		"static player_identity_status_e playerIdentityPrepareRuntimeBodyOnly");
	const std::string runtime = player_init_function(source,
		"player_identity_status_e playerIdentityPrepareRuntime(");

	REQUIRE(body_only.find("catalogGetBodyIsComplete(runtime_bodynum)") !=
		std::string::npos);
	REQUIRE(body_only.find("PLAYER_IDENTITY_UNBOUND_HEAD_RUNTIME_INDEX") !=
		std::string::npos);
	REQUIRE(body_only.find("candidate.runtime_headnum") == std::string::npos);
	REQUIRE(runtime.find("runtime_headnum == -1") != std::string::npos);
	REQUIRE(runtime.find("playerIdentityPrepareRuntimeBodyOnly") !=
		std::string::npos);
}

TEST_CASE("B-1064 Eyespy state publishes only after both candidates exist",
		"[player-init][eyespy][transaction][static][B-1064]")
{
	const std::string source = read_player_init_source("src/game/playerreset.c");
	const std::string body_source = read_player_init_source("src/game/body.c");
	const std::string discard_prop = player_init_function(source,
		"static void playerDiscardUnpublishedEyespyProp");
	const std::string prepare = player_init_function(source,
		"static bool playerPrepareEyespy");
	const std::string commit = player_init_function(source,
		"static void playerCommitEyespy");
	const std::string reset = player_init_function(source,
		"enum player_reset_result playerReset(void)");
	const std::string allocate = player_init_function(body_source,
		"struct prop *bodyAllocateEyespy");
	const size_t prop_allocate = prepare.find("bodyAllocateEyespy");
	const size_t prop_check = prepare.find("if (!prop)", prop_allocate);
	const size_t state_allocate = prepare.find("mempAlloc(sizeof(*candidate)", prop_check);
	const size_t state_check = prepare.find("if (!candidate)", state_allocate);
	const size_t candidate_ready = prepare.find("out_candidate->state = candidate", state_check);
	const size_t player_prepare = reset.find("playerPrepareEyespy");
	const size_t player_prop_allocate = reset.find("playerprop = propAllocate()", player_prepare);
	const size_t player_prop_publish = reset.find("currentplayer->prop = playerprop", player_prop_allocate);
	const size_t eyespy_commit = reset.find("playerCommitEyespy", player_prop_publish);
	const size_t discard_type = discard_prop.find("prop->type = PROPTYPE_OBJ");
	const size_t discard_free = discard_prop.find("propFree(prop)", discard_type);
	const size_t allocate_type = allocate.find("prop->type = PROPTYPE_OBJ");
	const size_t allocate_free = allocate.find("propFree(prop)", allocate_type);

	REQUIRE(prop_allocate != std::string::npos);
	REQUIRE(prop_check != std::string::npos);
	REQUIRE(state_allocate != std::string::npos);
	REQUIRE(state_check != std::string::npos);
	REQUIRE(candidate_ready != std::string::npos);
	REQUIRE(player_prepare != std::string::npos);
	REQUIRE(player_prop_allocate != std::string::npos);
	REQUIRE(player_prop_publish != std::string::npos);
	REQUIRE(eyespy_commit != std::string::npos);
	REQUIRE(prop_allocate < prop_check);
	REQUIRE(prop_check < state_allocate);
	REQUIRE(state_allocate < state_check);
	REQUIRE(state_check < candidate_ready);
	REQUIRE(player_prepare < player_prop_allocate);
	REQUIRE(player_prop_allocate < player_prop_publish);
	REQUIRE(player_prop_publish < eyespy_commit);
	REQUIRE(prepare.find("currentplayer->eyespy") == std::string::npos);
	REQUIRE(prepare.find("playerDiscardUnpublishedEyespyProp(prop)",
		state_check) != std::string::npos);
	REQUIRE(discard_prop.find("chrRemove(prop, true)") != std::string::npos);
	REQUIRE(discard_type != std::string::npos);
	REQUIRE(discard_free != std::string::npos);
	REQUIRE(discard_type < discard_free);
	REQUIRE(commit.find("currentplayer->eyespy = candidate->state") !=
		std::string::npos);
	REQUIRE(commit.find("s_PlayerEyespyNextPad = candidate->pad + 1") !=
		std::string::npos);
	REQUIRE(reset.find("playerDiscardEyespyCandidate(&eyespy_candidate)",
		player_prepare) != std::string::npos);
	REQUIRE(allocate.find("prop = propAllocate()") != std::string::npos);
	REQUIRE(allocate.find("chr0f020b14(prop, model") != std::string::npos);
	REQUIRE(allocate_type != std::string::npos);
	REQUIRE(allocate_free != std::string::npos);
	REQUIRE(allocate_type < allocate_free);
	REQUIRE(allocate.find("modelmgrFreeModel(model)") != std::string::npos);
	REQUIRE(allocate.find("chrAllocate(") == std::string::npos);
	REQUIRE(allocate.find("cheatIsActive(CHEAT_ENEMYSHIELDS)") !=
		std::string::npos);
	REQUIRE(allocate.find("chrSetShield(chr, 8)") != std::string::npos);
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

TEST_CASE("B-1076 ordinary Combat Simulator smoke owns two complete UI cycles",
		"[player-init][combat-sim][cycle][menu][static][b1076]")
{
	const std::string verifier = read_player_init_source(
		"port/src/combat_sim_verify.c");
	const std::string smoke = read_player_init_source(
		"tools/smoke-verify/tests/combat_sim_ui_release_cycle.json");
	const std::string init = player_init_function(
		verifier, "void combatSimVerifyInit(void)");

	/* This is deliberately the ordinary title -> Agent Select -> Main Menu ->
	 * Room route. The debug latch may press the production Start Match widget,
	 * but it may not seed or directly launch a match. */
	REQUIRE(smoke.find("--launch-mp-room") == std::string::npos);
	REQUIRE(smoke.find("--debug-auto-start-match") != std::string::npos);
	REQUIRE(smoke.find("--debug-combat-sim-cycles") != std::string::npos);
	REQUIRE(smoke.find("BOOT: --match-timelimit-sec armed: seconds=18") !=
		std::string::npos);
	REQUIRE(smoke.find("combat-sim-ui-cycle-home/agent_smoke.json") !=
		std::string::npos);
	REQUIRE(init.find(
		"!sysArgCheck(\"--no-net\") || !sysArgCheck(\"--debug-auto-start-match\")") !=
		std::string::npos);
	REQUIRE(init.find("!sysArgGetString(\"--launch-mp-room\")") ==
		std::string::npos);
	REQUIRE(init.find("start_mode=%s") != std::string::npos);

	REQUIRE(smoke.find("MENU\\\\.GRAPH\\\\.FIRE source=agent_select edge=load") !=
		std::string::npos);
	REQUIRE(smoke.find("MENU\\\\.GRAPH\\\\.FIRE source=main_menu edge=solo_play") !=
		std::string::npos);
	REQUIRE(smoke.find("MENU\\\\.GRAPH\\\\.FIRE source=main_solo_view edge=combat_simulator") !=
		std::string::npos);
	REQUIRE(smoke.find("MENU\\\\.GRAPH\\\\.FIRE source=room edge=start_match") !=
		std::string::npos);
	REQUIRE(smoke.find("MENU\\\\.GRAPH\\\\.FIRE source=endscreen_mp edge=continue") !=
		std::string::npos);
	REQUIRE(count_substring(smoke,
		"\"condition\": \"offline_gameplay_ready\"") == 2);
	REQUIRE(count_substring(smoke,
		"\"condition\": \"endscreen_visible\"") == 2);
	REQUIRE(smoke.find("stable_ms=2000 stable_elapsed_ms=") !=
		std::string::npos);
	REQUIRE(smoke.find("stable_ms=1000 stable_elapsed_ms=") !=
		std::string::npos);
	REQUIRE(count_substring(smoke,
		"SMOKE\\\\.WAIT: satisfied condition=offline_gameplay_ready") == 2);
	REQUIRE(count_substring(smoke,
		"SMOKE\\\\.WAIT: satisfied condition=endscreen_visible") == 2);

	REQUIRE(smoke.find(
		"\"pattern\": \"TRANSITION\\\\.STAGE\\\\.PREP reason='mpStartMatch' flags=0x02\", \"min\": 2, \"max\": 2") !=
		std::string::npos);
	REQUIRE(smoke.find(
		"\"pattern\": \"MENUPOOL: acquired room\", \"min\": 2, \"max\": 2") !=
		std::string::npos);
	REQUIRE(smoke.find(
		"\"pattern\": \"MENUPOOL: released room\", \"min\": 2, \"max\": 2") !=
		std::string::npos);
	REQUIRE(smoke.find("MENU: watchdog") != std::string::npos);
	REQUIRE(smoke.find("INPUTCTX watchdog") != std::string::npos);
	REQUIRE(smoke.find("legacy-stack leak") != std::string::npos);
	REQUIRE(smoke.find("SMOKE: result=wait_timeout") != std::string::npos);
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
