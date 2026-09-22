/*
 * Source-integrity and pure-helper behavior contracts for the bounded Combat
 * Simulator cohort B-249/B-242/B-174/B-183. These checks are not runtime fault
 * injection and do not replace the ordinary-client Car Park fixture.
 */

#include "catch.hpp"

#include <fstream>
#include <sstream>
#include <string>

extern "C" {
#include "mpstats_attribution.h"
#include "mpspawn_transaction.h"
}

static std::string cohortReadSource(const char *relative_path)
{
	const std::string path = std::string(PD_SOURCE_DIR) + "/" + relative_path;
	std::ifstream in(path, std::ios::in | std::ios::binary);
	REQUIRE(in.good());
	std::ostringstream text;
	text << in.rdbuf();
	return text.str();
}

TEST_CASE("B-249 death attribution converts runtime roster indices once",
		"[combat-sim][static][b249][index-domain]")
{
	const std::string source = cohortReadSource("src/game/mpstats.c");
	const std::string header = cohortReadSource("src/include/game/mpstats.h");
	const std::string player = cohortReadSource("src/game/player.c");
	const std::string chraction = cohortReadSource("src/game/chraction.c");
	const std::string netmsg = cohortReadSource("port/src/net/netmsg.c");

	REQUIRE(source.find("static bool mpstatsResolveActor(s32 runtime_index")
		!= std::string::npos);
	REQUIRE(source.find("mpstatsAttributionResolve(runtime_index, g_MpNumChrs")
		!= std::string::npos);
	REQUIRE(header.find("mpstatsRecordDeathByRuntimeIndex")
		!= std::string::npos);
	REQUIRE(player.find("mpstatsRecordDeathByRuntimeIndex(shooter_runtime_index,")
		!= std::string::npos);
	REQUIRE(player.find("victim_runtime_index = mpPlayerGetIndex(chr);")
		!= std::string::npos);
	REQUIRE(player.find("mpstatsAttributionChooseShooterRuntimeIndex(-1,")
		!= std::string::npos);
	REQUIRE(chraction.find("mpstatsRecordDeathByRuntimeIndex(aplayernum,")
		!= std::string::npos);
	REQUIRE(netmsg.find("#include \"mpstats_attribution.h\"")
		!= std::string::npos);
	REQUIRE(netmsg.find("victim_runtime_index = mpPlayerGetIndex(pl->prop->chr);")
		!= std::string::npos);
	REQUIRE(netmsg.find("mpstatsAttributionChooseShooterRuntimeIndex(")
		!= std::string::npos);
	REQUIRE(netmsg.find("playerDieByShooter(shooter_runtime_index, true);")
		!= std::string::npos);
	REQUIRE(netmsg.find("rejected live death with invalid runtime attribution")
		!= std::string::npos);
	REQUIRE(source.find("mpstatsConfigSlotHasPlayer(ampindex)")
		!= std::string::npos);
	REQUIRE(source.find("mpstatsConfigSlotHasPlayer(vmpindex)")
		!= std::string::npos);
	REQUIRE(source.find("vmpindex >= MAX_PLAYERS") != std::string::npos);
	REQUIRE(source.find("MPSTATS.ATTRIBUTION runtime_attacker=")
		!= std::string::npos);
	REQUIRE(source.find("attacker_runtime_index >= 0 && attacker_runtime_index < PLAYERCOUNT()")
		== std::string::npos);
	REQUIRE(source.find("ampindex < PLAYERCOUNT()") == std::string::npos);
	REQUIRE(source.find("vmpindex < PLAYERCOUNT()") == std::string::npos);
}

TEST_CASE("B-249 sparse players retain runtime and stable attribution domains",
		"[combat-sim][behavior][b249][index-domain]")
{
	mpstats_actor_identity_t identity;

	/* Runtime roster [player stable slot 3, bot stable slot 8]. */
	REQUIRE(mpstatsAttributionResolve(0, 2, 3, 8, 40, &identity));
	REQUIRE(identity.runtime_index == 0);
	REQUIRE(identity.config_slot == 3);
	REQUIRE(identity.kind == MPSTATS_ACTOR_PLAYER);

	REQUIRE(mpstatsAttributionResolve(1, 2, 8, 8, 40, &identity));
	REQUIRE(identity.runtime_index == 1);
	REQUIRE(identity.config_slot == 8);
	REQUIRE(identity.kind == MPSTATS_ACTOR_BOT);

	/* A sparse stable player slot passed as a runtime index fails closed. */
	REQUIRE_FALSE(mpstatsAttributionResolve(3, 2, 3, 8, 40, &identity));
	REQUIRE(identity.kind == MPSTATS_ACTOR_INVALID);

	/* Stable player slot 3 is outside the compact [0, 2) runtime roster. */
	REQUIRE(mpstatsAttributionChooseShooterRuntimeIndex(3, -1, 0, 2) == 0);
	REQUIRE(mpstatsAttributionChooseShooterRuntimeIndex(-1, -1, 0, 2) == 0);
	REQUIRE(mpstatsAttributionChooseShooterRuntimeIndex(1, -1, 0, 2) == 1);
	REQUIRE(mpstatsAttributionChooseShooterRuntimeIndex(-1, 1, 0, 2) == 1);
	REQUIRE(mpstatsAttributionChooseShooterRuntimeIndex(-1, -1, 3, 2) == -1);
}

TEST_CASE("B-242 human placement transaction cannot commit a partial prepare",
		"[combat-sim][behavior][b242][transaction]")
{
	mpspawn_transaction_t transaction;

	REQUIRE(mpspawnTransactionBegin(&transaction, 2) ==
		MPSPAWN_TRANSACTION_OK);
	REQUIRE(mpspawnTransactionRecordPrepare(&transaction, 3, 1) ==
		MPSPAWN_TRANSACTION_OK);
	REQUIRE(mpspawnTransactionRecordPrepare(&transaction, 0, 0) ==
		MPSPAWN_TRANSACTION_PREPARE_REJECTED);
	REQUIRE_FALSE(mpspawnTransactionCanCommit(&transaction));
	REQUIRE(mpspawnTransactionCommit(&transaction) ==
		MPSPAWN_TRANSACTION_PREPARE_REJECTED);
	REQUIRE_FALSE(transaction.committed);

	REQUIRE(mpspawnTransactionBegin(&transaction, 2) ==
		MPSPAWN_TRANSACTION_OK);
	REQUIRE(mpspawnTransactionRecordPrepare(&transaction, 3, 1) ==
		MPSPAWN_TRANSACTION_OK);
	REQUIRE_FALSE(mpspawnTransactionCanCommit(&transaction));
	REQUIRE(mpspawnTransactionRecordPrepare(&transaction, 0, 1) ==
		MPSPAWN_TRANSACTION_OK);
	REQUIRE(mpspawnTransactionCanCommit(&transaction));
	REQUIRE(mpspawnTransactionCommit(&transaction) ==
		MPSPAWN_TRANSACTION_OK);
	REQUIRE(transaction.committed);
}

TEST_CASE("B-242 converged prepared capsules reject the whole transaction",
		"[combat-sim][behavior][b242][transaction][capsule]")
{
	mpspawn_transaction_t transaction;
	mpspawn_capsule_t a = {100.0f, 200.0f, 10.0f, 30.0f, 185.0f};
	mpspawn_capsule_t b = {100.0f, 200.0f, 20.0f, 30.0f, 185.0f};

	REQUIRE(mpspawnTransactionBegin(&transaction, 2) ==
		MPSPAWN_TRANSACTION_OK);
	REQUIRE(mpspawnTransactionRecordPrepare(&transaction, 0, 1) ==
		MPSPAWN_TRANSACTION_OK);
	REQUIRE(mpspawnTransactionRecordPrepare(&transaction, 3, 1) ==
		MPSPAWN_TRANSACTION_OK);
	REQUIRE(mpspawnCapsulesOverlap(&a, &b));
	REQUIRE(mpspawnTransactionRejectPreparedBatch(&transaction) ==
		MPSPAWN_TRANSACTION_BATCH_REJECTED);
	REQUIRE_FALSE(mpspawnTransactionCanCommit(&transaction));
	REQUIRE(mpspawnTransactionCommit(&transaction) ==
		MPSPAWN_TRANSACTION_BATCH_REJECTED);
	REQUIRE_FALSE(transaction.committed);

	/* Touching in XZ or vertically does not create volume overlap. */
	b.x = 160.0f;
	REQUIRE_FALSE(mpspawnCapsulesOverlap(&a, &b));
	b.x = 100.0f;
	b.bottom_y = 195.0f;
	REQUIRE_FALSE(mpspawnCapsulesOverlap(&a, &b));
}

TEST_CASE("B-242 temporary MP pool scan reaches later clear entries and exhausts",
		"[combat-sim][behavior][b242][pool-scan]")
{
	mpspawn_pool_scan_t scan;

	REQUIRE(mpspawnPoolScanBegin(&scan, 3));
	REQUIRE(mpspawnPoolScanNext(&scan) == 0); /* blocked */
	REQUIRE(mpspawnPoolScanNext(&scan) == 1); /* clear */
	REQUIRE(mpspawnPoolScanAccept(&scan, 1));
	REQUIRE(mpspawnPoolScanResult(&scan) == 1);
	REQUIRE(mpspawnPoolScanNext(&scan) == -1);

	REQUIRE(mpspawnPoolScanBegin(&scan, 2));
	REQUIRE(mpspawnPoolScanNext(&scan) == 0); /* blocked */
	REQUIRE(mpspawnPoolScanNext(&scan) == 1); /* blocked */
	REQUIRE(mpspawnPoolScanNext(&scan) == -1);
	REQUIRE(mpspawnPoolScanResult(&scan) == -1);
}

TEST_CASE("B-242 every player and bot spawn consumes one capsule policy",
		"[combat-sim][static][b242][spawn]")
{
	const std::string policy = cohortReadSource("src/game/spawnpool.c");
	const std::string header = cohortReadSource("src/include/game/spawnpool.h");
	const std::string player = cohortReadSource("src/game/player.c");
	const std::string player_header = cohortReadSource("src/include/game/player.h");
	const std::string player_reset = cohortReadSource("src/game/playerreset.c");
	const std::string bot = cohortReadSource("src/game/bot.c");
	const std::string orchestrator =
		cohortReadSource("src/game/mplayer/mpspawn_orchestrate.c");
	const std::string orchestrator_header = cohortReadSource(
		"src/include/game/mplayer/mpspawn_orchestrate.h");
	const std::string lv = cohortReadSource("src/game/lv.c");

	REQUIRE(header.find("struct prop *selfprop") != std::string::npos);
	REQUIRE(policy.find("CDTYPE_ALL, CHECKVERTICAL_YES, height, 0.0f")
		!= std::string::npos);
	REQUIRE(policy.find("capsuleStage2RayCast") != std::string::npos);
	REQUIRE(policy.find("restore_self_perim") != std::string::npos);
	REQUIRE(policy.find("SPAWN.CLIP:") != std::string::npos);
	REQUIRE(player.find("spawnPoolFindClearPosition(&pos, rooms, chr_radius, chr_height,")
		!= std::string::npos);
	REQUIRE(player_reset.find("PLAYER_RESET_SPAWN_FAILED")
		!= std::string::npos);
	REQUIRE(player_reset.find("including the temporary point")
		!= std::string::npos);
	REQUIRE(player_reset.find("mpspawnPoolScanNext(&scan)")
		!= std::string::npos);
	REQUIRE(player_reset.find("temporary_pool_all_blocked")
		!= std::string::npos);
	REQUIRE(player_reset.find("spawnCapsuleValidated = true;")
		!= std::string::npos);
	REQUIRE(bot.find("spawnPoolFindClearPosition(&pos, rooms, chr_radius,")
		!= std::string::npos);
	REQUIRE(player_header.find("playerPrepareOrchestratedSpawnFromPool")
		!= std::string::npos);
	REQUIRE(player.find("playernum < 0") != std::string::npos);
	REQUIRE(player.find("playernum >= MAX_PLAYERS") != std::string::npos);
	REQUIRE(player.find("player->prop->chr == NULL") != std::string::npos);
	REQUIRE(player.find("plan->prop->chr != NULL") != std::string::npos);
	REQUIRE(orchestrator.find("mpspawnTransactionCanCommit(&human_transaction)")
		!= std::string::npos);
	REQUIRE(orchestrator.find("mpspawnCapsulesOverlap(&a, &b)")
		!= std::string::npos);
	REQUIRE(orchestrator.find("mpspawnTransactionRejectPreparedBatch(")
		!= std::string::npos);
	REQUIRE(orchestrator.find("playerCommitValidatedOrchestratedSpawn(&human_plans[i])")
		!= std::string::npos);
	REQUIRE(orchestrator.find("reason=no_pool_assignment")
		!= std::string::npos);
	REQUIRE(player_reset.find("playerRollbackStageInitialization();")
		!= std::string::npos);
	REQUIRE(player_reset.find("residual_prop=%d residual_eyespy=%d residual_chrbody=%d")
		!= std::string::npos);
	REQUIRE(orchestrator_header.find(
		"enum mp_orchestrate_spawn_result mpOrchestrateMatchStartSpawns(void)")
		!= std::string::npos);
	REQUIRE(lv.find("orchestrate_result != MP_ORCHESTRATE_SPAWN_OK")
		!= std::string::npos);
	REQUIRE(lv.find("lvRollbackCommittedPlayers(committed_player_order,")
		!= std::string::npos);
	REQUIRE(bot.find("fallback=live_bounded") != std::string::npos);

	const size_t rejection = orchestrator.find("reason=prepare status=");
	const size_t human_commit =
		orchestrator.find("playerCommitValidatedOrchestratedSpawn(&human_plans[i])");
	const size_t batch_rejection =
		orchestrator.find("reason=prepared_capsule_overlap");
	const size_t publication =
		orchestrator.find("g_MpOrchestrateInitialSpawnDone = true;");
	REQUIRE(rejection < human_commit);
	REQUIRE(batch_rejection < human_commit);
	REQUIRE(human_commit < publication);
	REQUIRE(rejection < publication);
	REQUIRE(batch_rejection < publication);

	const size_t commit_begin = player.find(
		"void playerCommitValidatedOrchestratedSpawn(");
	const size_t commit_end = player.find(
		"static bool playerTrySelectPoolSpawn", commit_begin);
	REQUIRE(commit_begin != std::string::npos);
	REQUIRE(commit_end != std::string::npos);
	const std::string commit = player.substr(commit_begin,
		commit_end - commit_begin);
	REQUIRE(commit.find("playerOrchestratedSpawnPlanCanCommit") ==
		std::string::npos);
	REQUIRE(commit.find("commit rejected") == std::string::npos);
	REQUIRE(commit.find("if (") == std::string::npos);
	REQUIRE(commit.find("return;") == std::string::npos);
}

TEST_CASE("B-174 initial bot move rejects before spawn publication",
		"[combat-sim][static][b174][bot-spawn]")
{
	const std::string source = cohortReadSource("src/game/bot.c");
	const std::string chraction = cohortReadSource("src/game/chraction.c");
	const std::string header = cohortReadSource("src/include/game/bot.h");
	const std::string ai = cohortReadSource(
		"src/game/mplayer/mpaicommands.c");
	const std::string botmgr = cohortReadSource("src/game/botmgr.c");
	const size_t attempt_begin = source.find(
		"static enum bot_spawn_result botSpawnAttempt");
	const size_t attempt_end = source.find("\nvoid botSpawn(", attempt_begin);
	REQUIRE(attempt_begin != std::string::npos);
	REQUIRE(attempt_end != std::string::npos);
	const std::string attempt = source.substr(attempt_begin,
		attempt_end - attempt_begin);
	const size_t move_rejection = attempt.find(
		"if (!chrMoveToPos(chr, &pos, rooms, thing, true))");
	const size_t cache_consumption = attempt.find(
		"g_MpOrchestrateBotPoolIdx[aibot->aibotnum] = -1;");
	const size_t child_deletion = attempt.find(
		"obj->hidden |= OBJHFLAG_DELETING;");
	const size_t bot_reset = attempt.find("botReset(chr, respawning);");
	const size_t splat_reset = attempt.find("splatResetChr(chr);");
	const size_t ai_initialization = attempt.find(
		"chr->aibot->roty = modelGetChrRotY(chr->model);");
	const size_t stuck_publication = attempt.find(
		"s_BotStuck[slot].snapshot = chr->prop->pos;");

	REQUIRE(attempt.find("s32 cached_pool_idx = -1;")
		!= std::string::npos);
	REQUIRE(attempt.find("u32 previous_hidden = chr->hidden;")
		!= std::string::npos);
	REQUIRE(attempt.find("chr->hidden = previous_hidden;")
		!= std::string::npos);
	REQUIRE(attempt.find("return BOT_SPAWN_MOVE_REJECTED;")
		!= std::string::npos);
	REQUIRE(move_rejection != std::string::npos);
	REQUIRE(cache_consumption != std::string::npos);
	REQUIRE(child_deletion != std::string::npos);
	REQUIRE(bot_reset != std::string::npos);
	REQUIRE(splat_reset != std::string::npos);
	REQUIRE(move_rejection < cache_consumption);
	REQUIRE(move_rejection < child_deletion);
	REQUIRE(move_rejection < bot_reset);
	REQUIRE(move_rejection < splat_reset);
	REQUIRE(move_rejection < ai_initialization);
	REQUIRE(move_rejection < stuck_publication);

	const size_t public_begin = source.find("void botSpawn(", attempt_end);
	const size_t public_end = source.find(
		"\nenum bot_spawn_wave_result botSpawnAll(", public_begin);
	REQUIRE(public_begin != std::string::npos);
	REQUIRE(public_end != std::string::npos);
	const std::string public_boundary = source.substr(public_begin,
		public_end - public_begin);
	REQUIRE(public_boundary.find("spawn_result = botSpawnAttempt")
		!= std::string::npos);
	REQUIRE(public_boundary.find("spawn_result != BOT_SPAWN_OK")
		!= std::string::npos);
	REQUIRE(public_boundary.find("retry_state=preserved")
		!= std::string::npos);
	REQUIRE(public_boundary.find("(void)botSpawnAttempt") ==
		std::string::npos);
	REQUIRE(chraction.find("next dead tick deliberately")
		!= std::string::npos);
	REQUIRE(source.find("invalid-room tick retries")
		!= std::string::npos);
	REQUIRE(source.find("same room-recovery retry predicate")
		!= std::string::npos);

	const size_t wave_begin = source.find(
		"enum bot_spawn_wave_result botSpawnAll(", public_end);
	const size_t wave_end = source.find("\nu32 add87654321", wave_begin);
	REQUIRE(wave_begin != std::string::npos);
	REQUIRE(wave_end != std::string::npos);
	const std::string wave = source.substr(wave_begin, wave_end - wave_begin);
	const size_t spawn_attempt = wave.find(
		"botSpawnAttempt(g_MpBotChrPtrs[i], false);");
	const size_t first_failure = wave.find("stop=first next_uncommitted=");
	const size_t cursor_advance = wave.find(
		"s_BotSpawnWaveNextUncommitted = i + 1;");
	const size_t completion_guard = wave.find(
		"if (s_BotSpawnWaveNextUncommitted == g_BotCount)");
	const size_t suffix_loop = wave.find(
		"for (i = s_BotSpawnWaveNextUncommitted; i < g_BotCount; i++)");
	REQUIRE(first_failure != std::string::npos);
	REQUIRE(spawn_attempt != std::string::npos);
	REQUIRE(cursor_advance != std::string::npos);
	REQUIRE(completion_guard != std::string::npos);
	REQUIRE(suffix_loop != std::string::npos);
	REQUIRE(wave.find("rejected_count") == std::string::npos);
	REQUIRE(wave.find("Do not clear orchestration assignments here.")
		!= std::string::npos);
	REQUIRE(completion_guard < suffix_loop);
	REQUIRE(completion_guard < wave.find(
		"return BOT_SPAWN_WAVE_COMPLETE;", completion_guard));
	REQUIRE(spawn_attempt < first_failure);
	REQUIRE(first_failure < wave.find(
		"return BOT_SPAWN_WAVE_RETRY_PENDING;", first_failure));
	REQUIRE(first_failure < cursor_advance);
	REQUIRE(cursor_advance < wave.find(
		"return BOT_SPAWN_WAVE_COMPLETE;", cursor_advance));
	REQUIRE(wave.find("s_BotSpawnWaveNextUncommitted =",
		cursor_advance + 1) == std::string::npos);
	REQUIRE(wave.find("s_BotSpawnAllHealthy") == std::string::npos);

	REQUIRE(header.find("enum bot_spawn_wave_result") != std::string::npos);
	REQUIRE(header.find(
		"enum bot_spawn_wave_result botSpawnAll(void);")
		!= std::string::npos);
	REQUIRE(header.find("void botSpawnWaveReset(void);")
		!= std::string::npos);
	REQUIRE(ai.find(
		"if (botSpawnAll() != BOT_SPAWN_WAVE_COMPLETE)")
		!= std::string::npos);
	REQUIRE(ai.find("return false;") < ai.find("g_Vars.aioffset += 2;"));
	REQUIRE(botmgr.find("botSpawnWaveReset();") != std::string::npos);
	REQUIRE(botmgr.find("botSpawnWaveReset();") < botmgr.find("g_BotCount = 0;"));
	const size_t reset_begin = source.find("void botSpawnWaveReset(void)");
	const size_t reset_end = source.find(
		"static bool botFindStuckRecovery", reset_begin);
	REQUIRE(reset_begin != std::string::npos);
	REQUIRE(reset_end != std::string::npos);
	const std::string reset = source.substr(reset_begin,
		reset_end - reset_begin);
	REQUIRE(reset.find("s_BotSpawnWaveNextUncommitted = 0;")
		!= std::string::npos);
	REQUIRE(reset.find("s_BotSpawnFailsafeDone = false;")
		!= std::string::npos);
	REQUIRE(reset.find("s_BotSpawnFailsafeRetryPending = false;")
		!= std::string::npos);

	const size_t failsafe_call = source.find(
		"wave_result = botSpawnAll();", wave_end);
	const size_t failsafe_check = source.find(
		"wave_result != BOT_SPAWN_WAVE_COMPLETE", failsafe_call);
	const size_t failsafe_pending_trigger = source.find(
		"&& (s_BotSpawnFailsafeRetryPending", wave_end);
	REQUIRE(failsafe_call != std::string::npos);
	REQUIRE(failsafe_check != std::string::npos);
	REQUIRE(failsafe_pending_trigger != std::string::npos);
	REQUIRE(failsafe_pending_trigger < failsafe_call);
	REQUIRE(failsafe_call < failsafe_check);
	REQUIRE(source.find("s_BotSpawnFailsafeRetryPending = true;",
		failsafe_check) != std::string::npos);
	REQUIRE(source.find("g_Vars.lvframe60 == 0") == std::string::npos);
	REQUIRE(source.find("s_BotSpawnAllHealthy") == std::string::npos);
}

TEST_CASE("B-174 recovery is finite deterministic separated and arena neutral",
		"[combat-sim][static][b174][bot-ai]")
{
	const std::string source = cohortReadSource("src/game/bot.c");
	const size_t begin = source.find("static bool botFindStuckRecovery");
	const size_t end = source.find("static inline bool botShouldTickAI", begin);
	REQUIRE(begin != std::string::npos);
	REQUIRE(end != std::string::npos);
	const std::string recovery = source.substr(begin, end - begin);

	REQUIRE(recovery.find("STUCK_MAX_WAYPOINTS") != std::string::npos);
	REQUIRE(recovery.find("(slot + offset) % numwaypoints")
		!= std::string::npos);
	REQUIRE(recovery.find("spawnPoolFindClearPosition") != std::string::npos);
	REQUIRE(recovery.find("STUCK_RELO_SEPARATION_SQ") != std::string::npos);
	REQUIRE(recovery.find("slot >= MAX_BOTS") != std::string::npos);
	REQUIRE(recovery.find("rngRandom") == std::string::npos);
	REQUIRE(recovery.find("CARPARK") == std::string::npos);
	REQUIRE(recovery.find("Car Park") == std::string::npos);
	REQUIRE(source.find("aibot->numwaystepstotarget = 0;")
		!= std::string::npos);
	REQUIRE(source.find("if (chrMoveToPos(chr, &recovery, recoveryrooms, 0,")
		!= std::string::npos);
	REQUIRE(source.find("bs->relocating = 0;") != std::string::npos);
}

TEST_CASE("B-183 exact Dark Combat identity and headspot fail closed",
		"[combat-sim][static][b183][identity][headspot]")
{
	const std::string bodies = cohortReadSource("port/src/bodydata_authored.c");
	const std::string heads = cohortReadSource("port/src/headdata_authored.c");
	const std::string identity = cohortReadSource("port/src/player_identity.c");
	const std::string body = cohortReadSource("src/game/body.c");

	REQUIRE(bodies.find("{ \"base:dark_combat\",  86,") != std::string::npos);
	REQUIRE(heads.find("{ \"base:head_dark_combat\",   4,") != std::string::npos);
	REQUIRE(identity.find("PLAYER_IDENTITY_EMPTY_BODY_RIG_CLASS")
		!= std::string::npos);
	REQUIRE(identity.find("PLAYER_IDENTITY_EMPTY_HEAD_RIG_CLASS")
		!= std::string::npos);
	REQUIRE(identity.find("PLAYER_IDENTITY_INCOMPATIBLE_RIG_CLASS")
		!= std::string::npos);
	REQUIRE(body.find("BODY.HEADSPOT.REJECT: body_id=") != std::string::npos);
	REQUIRE(body.find("BODY.HEADSPOT.RESOLVED: body_id=") != std::string::npos);
	REQUIRE(body.find("headspot resolution") != std::string::npos);
	REQUIRE(body.find("missing headspot for bodynum") == std::string::npos);
}
