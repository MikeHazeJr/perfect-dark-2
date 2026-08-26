/*
 * Static source contracts for T-ENGINE-004/B-1067 prepared-stage rollback.
 *
 * These paths are coupled to the full game runtime, so the focused test pins
 * the ownership and ordering boundaries in the canonical C sources.
 */

#include "catch.hpp"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <sstream>
#include <string>

#ifndef PD_SOURCE_DIR
#error "PD_SOURCE_DIR must identify the canonical Perfect Dark source checkout"
#endif

namespace {

struct source_block {
	size_t begin;
	size_t open;
	size_t end;
	std::string text;
};

std::string read_pd_source(const char *relative_path)
{
	const std::filesystem::path path =
		std::filesystem::path(PD_SOURCE_DIR) / relative_path;
	INFO("source path: " << path.string());

	std::ifstream input(path, std::ios::in | std::ios::binary);
	REQUIRE(input.good());

	std::ostringstream output;
	output << input.rdbuf();
	return output.str();
}

std::string remove_comments(const std::string &text)
{
	std::string result;
	result.reserve(text.size());
	bool line_comment = false;
	bool block_comment = false;
	bool quoted = false;
	bool character = false;
	bool escaped = false;

	for (size_t i = 0; i < text.size(); ++i) {
		const char current = text[i];
		const char next = i + 1 < text.size() ? text[i + 1] : '\0';

		if (line_comment) {
			if (current == '\n') {
				line_comment = false;
				result.push_back(current);
			}
			continue;
		}

		if (block_comment) {
			if (current == '*' && next == '/') {
				block_comment = false;
				++i;
			}
			continue;
		}

		if (!quoted && !character && current == '/' && next == '/') {
			line_comment = true;
			++i;
			continue;
		}

		if (!quoted && !character && current == '/' && next == '*') {
			block_comment = true;
			++i;
			continue;
		}

		result.push_back(current);

		if (escaped) {
			escaped = false;
			continue;
		}

		if ((quoted || character) && current == '\\') {
			escaped = true;
		} else if (!character && current == '"') {
			quoted = !quoted;
		} else if (!quoted && current == '\'') {
			character = !character;
		}
	}

	return result;
}

std::string compact_source(const std::string &text)
{
	std::string compact;
	compact.reserve(text.size());

	for (const unsigned char character : remove_comments(text)) {
		if (!std::isspace(character)) {
			compact.push_back(static_cast<char>(character));
		}
	}

	return compact;
}

void require_text(const std::string &text, const char *needle,
		const char *contract)
{
	INFO("contract: " << contract);
	INFO("missing source text: " << needle);
	REQUIRE(text.find(needle) != std::string::npos);
}

size_t count_text(const std::string &text, const char *needle)
{
	size_t count = 0;
	size_t position = 0;

	while ((position = text.find(needle, position)) != std::string::npos) {
		++count;
		position += std::string(needle).size();
	}

	return count;
}

size_t first_of(const std::string &text,
		std::initializer_list<const char *> needles, size_t after = 0)
{
	size_t first = std::string::npos;

	for (const char *needle : needles) {
		const size_t position = text.find(needle, after);
		if (position != std::string::npos
				&& (first == std::string::npos || position < first)) {
			first = position;
		}
	}

	return first;
}

source_block block_at(const std::string &text, const char *marker,
		size_t search_from = 0)
{
	const size_t begin = text.find(marker, search_from);
	INFO("block marker: " << marker);
	REQUIRE(begin != std::string::npos);

	const size_t open = text.find('{', begin);
	REQUIRE(open != std::string::npos);

	size_t depth = 0;
	for (size_t position = open; position < text.size(); ++position) {
		if (text[position] == '{') {
			++depth;
		} else if (text[position] == '}') {
			REQUIRE(depth > 0);
			if (--depth == 0) {
				return {begin, open, position + 1,
					text.substr(begin, position - begin + 1)};
			}
		}
	}

	FAIL("unterminated source block");
	return {};
}

source_block definition_block(const std::string &text, const char *signature)
{
	size_t search_from = 0;

	while (true) {
		const size_t begin = text.find(signature, search_from);
		INFO("function signature: " << signature);
		REQUIRE(begin != std::string::npos);

		const size_t open = text.find('{', begin);
		REQUIRE(open != std::string::npos);

		const size_t semicolon = text.find(';', begin);
		if (semicolon != std::string::npos && semicolon < open) {
			search_from = semicolon + 1;
			continue;
		}

		return block_at(text, signature, begin);
	}
}

void require_order(const std::string &text,
		std::initializer_list<const char *> markers,
		const char *contract)
{
	size_t previous = 0;
	bool first = true;

	for (const char *marker : markers) {
		const size_t current = text.find(marker, first ? 0 : previous + 1);
		INFO("contract: " << contract);
		INFO("ordered source marker: " << marker);
		REQUIRE(current != std::string::npos);
		if (!first) {
			REQUIRE(previous < current);
		}
		previous = current;
		first = false;
	}
}

} /* namespace */

TEST_CASE("validated client settings abort an exact prepared participant before publication",
		"[net][ready-gate][transaction][static][B-1067][T-ENGINE-004]")
{
	const std::string source = compact_source(
		read_pd_source("port/src/net/netmsg.c"));
	const source_block handler = definition_block(source,
		"u32netmsgClcSettingsRead(structnetbuf*src,structnetclient*srccl)");
	const source_block participant = definition_block(source,
		"statics32readyGatePreparingClientIndex(conststructnetclient*client)");
	const source_block malformed = block_at(handler.text,
		"if(status!=NET_CLIENT_SETTINGS_WIRE_OK)");
	const source_block prepared_race = block_at(handler.text,
		"if(readyGatePreparingClientIndex(srccl)>=0)", malformed.end);

	const size_t wire_read = handler.text.find(
		"netClientSettingsWireRead(src,&plan,&identity_status)");
	const size_t abort = prepared_race.text.find("readyGateAbort(");
	const size_t sanitize = handler.text.find(
		"netmsgSanitizeClientTeam(srccl,plan.team)", prepared_race.end);
	const size_t first_publication = first_of(handler.text, {
		"strncpy(srccl->settings.body_id",
		"strncpy(srccl->settings.head_id",
		"strncpy(srccl->settings.name",
		"srccl->settings.options=",
		"srccl->settings.fovy=",
		"srccl->settings.fovzoommult=",
		"srccl->settings.handicap=",
		"srccl->settings.team="
	});

	INFO("contract: the complete typed wire plan is validated before race handling");
	REQUIRE(wire_read != std::string::npos);
	REQUIRE(wire_read < malformed.begin);
	REQUIRE(malformed.end <= prepared_race.begin);
	require_text(participant.text, "s_ReadyGate.active",
		"ready-gate participation requires an active transaction");
	require_text(participant.text, "s_ReadyGate.expected_mask",
		"the race applies only to an expected ready-gate participant");
	require_text(participant.text, "&g_NetClients[i]==client",
		"participation is resolved by exact source-client identity");
	require_text(participant.text, "client->state==CLSTATE_PREPARING",
		"only a client owned by the prepared launch can abort it");
	require_text(prepared_race.text, "readyGateAbort(",
		"a valid settings change aborts and rolls back the prepared launch");
	INFO("contract: a valid settings race is neither a kick nor a parser failure");
	REQUIRE(prepared_race.text.find("netServerKick(") == std::string::npos);
	REQUIRE(prepared_race.text.find("return") == std::string::npos);

	INFO("contract: rollback precedes team recomputation and all settings publication");
	REQUIRE(abort != std::string::npos);
	REQUIRE(sanitize != std::string::npos);
	REQUIRE(first_publication != std::string::npos);
	REQUIRE(prepared_race.begin + abort < sanitize);
	REQUIRE(sanitize < first_publication);
}

TEST_CASE("ready-gate settings smoke uses one ordinary validated client send",
		"[net][ready-gate][transaction][smoke-seam][static][B-1103][T-ENGINE-004]")
{
	const std::string main_source = compact_source(
		read_pd_source("port/src/main.c"));
	const std::string tick_source = compact_source(
		read_pd_source("port/src/pdmain.c"));
	const source_block boot = definition_block(main_source,
		"staticvoidbootApplyCliFastPaths(void)");
	const source_block settings_tick = definition_block(main_source,
		"s32bootReadyGateSettingsChangeTick(void)");
	const source_block main_tick = definition_block(tick_source,
		"voidmainTick(void)");

	require_order(boot.text,
		{"sysArgCheck(\"--debug-ready-gate-settings-change\")",
			"if(!smokeHarnessIsActive())",
			"g_BootReadyGateSettingsChangeArmed=1;"},
		"the race seam is armed only by an active smoke");
	require_text(settings_tick.text,
		"g_NetMode!=NETMODE_CLIENT||g_NetLocalClient==NULL||"
		"g_NetLocalClient->state!=CLSTATE_PREPARING",
		"the update waits for the exact ordinary remote-client prepare state");
	require_order(settings_tick.text,
		{"candidate_handicap=", "g_PlayerConfigsArray[0].handicap=candidate_handicap;",
			"g_BootReadyGateSettingsChangeFired=1;",
			"g_BootReadyGateSettingsChangeArmed=0;",
			"rc=netClientSettingsChanged();"},
		"a valid changed setting is frozen as one-shot before the ordinary send");
	require_text(settings_tick.text,
		"prior_handicap==0?0x80:(prior_handicap==0xff?0xfe:prior_handicap+1)",
		"every candidate sent by the seam is a valid nonzero handicap");
	INFO("contract: the seam cannot bypass the production settings writer");
	REQUIRE(settings_tick.text.find("netmsgClcSettingsWrite(") == std::string::npos);
	REQUIRE(settings_tick.text.find("netSend(") == std::string::npos);
	require_text(main_tick.text, "bootReadyGateSettingsChangeTick();",
		"the seam runs in the normal main frame between network receive and flush");
}

TEST_CASE("stage player initialization uses canonical identity and rolls back actual commit order",
		"[player-init][stage-load][transaction][static][B-1067][B-1104][T-ENGINE-004]")
{
	const std::string lv_source = compact_source(
		read_pd_source("src/game/lv.c"));
	const std::string playermgr_source = compact_source(
		read_pd_source("src/game/playermgr.c"));
	const std::string reset_header = compact_source(
		read_pd_source("src/include/game/playerreset.h"));
	const source_block lv_reset = definition_block(lv_source,
		"boollvReset(s32stagenum)");
	const source_block stage_order = definition_block(playermgr_source,
		"s32playermgrBuildStageInitOrder(s32out_order[MAX_PLAYERS])");
	const source_block rollback = definition_block(lv_source,
		"statics32lvRollbackCommittedPlayers("
		"consts32committed_order[MAX_PLAYERS],s32committed_count,"
		"constchar*failure_phase,s32failed_player,constchar*status)");
	const source_block reset_failure = block_at(lv_reset.text,
		"if(player_result!=PLAYER_RESET_OK)");
	const source_block spawn_failure = block_at(lv_reset.text,
		"if(chrbody_result!=PLAYER_CHRBODY_OK)");

	require_text(stage_order.text,
		"constboolnetwork_order=g_NetMode==NETMODE_SERVER||"
		"g_NetMode==NETMODE_CLIENT;",
		"only network matches replace endpoint-local index ordering");
	require_text(stage_order.text, "order_key=(s32)player->client->id;",
		"authenticated client ID is the canonical network ordering key");
	require_text(stage_order.text, "if(order_keys[i]==order_key)",
		"duplicate canonical identities fail before stage publication");
	require_text(stage_order.text,
		"while(insert_at>0&&order_keys[insert_at-1]>order_key)",
		"the immutable order is sorted independently of runtime player slots");
	require_text(lv_reset.text,
		"stage_player_count=playermgrBuildStageInitOrder(stage_player_order);",
		"lvReset freezes one validated order before any player reset");
	INFO("contract: reset and spawn consume that same immutable order");
	REQUIRE(count_text(lv_reset.text,
		"i=stage_player_order[order_position];") == 2);
	require_text(lv_reset.text,
		"s32committed_player_order[MAX_PLAYERS]={0};",
		"lvReset owns an exact initially-empty commit sequence");
	require_text(lv_reset.text,
		"committed_player_order[committed_player_count++]=i;",
		"each successful playerReset appends its real commit position");
	INFO("contract: successful reset tracking follows its failure branch");
	REQUIRE(reset_failure.end <= lv_reset.text.find(
		"committed_player_order[committed_player_count++]=i;"));
	REQUIRE(lv_reset.text.find(
		"committed_player_order[committed_player_count++]=i;") <
		spawn_failure.begin);

	for (const source_block *failure : {&reset_failure, &spawn_failure}) {
		require_order(failure->text,
			{"lvRollbackCommittedPlayers(committed_player_order,",
				"modelmgrSetLvResetting(false);", "returnfalse;"},
			"both failure classes cross the same rollback boundary before return");
		REQUIRE(count_text(failure->text,
			"lvRollbackCommittedPlayers(") == 1);
	}

	require_text(reset_header,
		"u32playerRollbackStageInitialization(void);",
		"lvReset delegates exact player ownership to the player-reset subsystem");
	require_text(rollback.text,
		"for(s32position=committed_count-1;position>=0;position--)",
		"the boundary reverses the real commit sequence, not local player numbers");
	require_text(rollback.text,
		"consts32playernum=committed_order[position];",
		"each rollback position resolves its exact runtime player");
	require_order(rollback.text,
		{"setCurrentPlayerNum(playernum);", "playerRollbackStageInitialization();"},
		"the boundary selects each exact player before invoking one teardown owner");
	REQUIRE(rollback.text.find("MAX_PLAYERS-1") == std::string::npos);
}

TEST_CASE("spawn orchestration keys humans by the same canonical stage identity",
		"[player-init][spawn][counterop][static][B-1104][T-ENGINE-004]")
{
	const std::string source = compact_source(
		read_pd_source("src/game/mplayer/mpspawn_orchestrate.c"));
	const source_block orchestrate = definition_block(source,
		"voidmpOrchestrateMatchStartSpawns(void)");
	const source_block compare = definition_block(source,
		"staticintorch_part_cmp(constvoid*va,constvoid*vb)");

	require_text(orchestrate.text,
		"stage_player_count=playermgrBuildStageInitOrder(stage_player_order);",
		"spawn assignment consumes the shared validated stage order");
	require_order(orchestrate.text,
		{"i=stage_player_order[order_position];",
			"s_Parts[s_PartCount].playernum=i;",
			"s_Parts[s_PartCount].orderkey=order_position;"},
		"each human retains its endpoint-local slot but receives a canonical sort key");
	require_text(compare.text, "ka=a->orderkey;",
		"the deterministic comparator no longer sorts humans by playernum");
	require_text(compare.text, "kb=b->orderkey;",
		"both comparator operands use canonical order");
	REQUIRE(compare.text.find("playernum") == std::string::npos);
	require_text(orchestrate.text,
		"SPAWN.ORCH:applyplayer=%dclient=%drole=%spool=%dduplicate=%d",
		"production logs expose semantic role to pool mapping on both endpoints");
}

TEST_CASE("stage player rollback retires every published runtime owner",
		"[player-init][ownership][rollback][static][B-1067][T-ENGINE-004]")
{
	const std::string source = compact_source(
		read_pd_source("src/game/playerreset.c"));
	const source_block owner = definition_block(source,
		"u32playerRollbackStageInitialization(void)");

	require_text(owner.text, "structplayer*player=g_Vars.currentplayer;",
		"rollback owns the exact player selected by the stage boundary");
	require_order(owner.text,
		{"eyespyprop=player->eyespy!=NULL?player->eyespy->prop:NULL;",
			"playerDiscardUnpublishedEyespyProp(eyespyprop);",
			"player->eyespy=NULL;"},
		"committed Eyespy prop ownership is retired before pointer publication clears");
	require_text(owner.text, "if(playerprop->chr!=NULL)",
		"both full and model-less character ownership enter character teardown");
	require_text(owner.text, "propDeregisterRooms(playerprop);",
		"a player prop without chr ownership still leaves room registration");
	require_order(owner.text,
		{"chrRemove(playerprop,true);", "propDelist(playerprop);",
			"propDisable(playerprop);", "propFree(playerprop);",
			"player->prop=NULL;"},
		"player chr, room, scheduler, allocation, and pointer ownership unwind together");
	require_text(owner.text,
		"g_MpAllChrPtrs[g_Vars.currentplayernum]=NULL;",
		"rollback clears the published MP character owner");
	require_text(owner.text,
		"g_MpAllChrConfigPtrs[g_Vars.currentplayernum]=NULL;",
		"rollback clears the published MP character-config owner");
	require_order(owner.text,
		{"if(player->gunmem2!=NULL)", "bgunFreeGunMem();",
			"player->gunmem2=NULL;"},
		"rollback releases stage-acquired gun memory before clearing ownership");
	require_text(owner.text, "player->model00d4=NULL;",
		"rollback clears the published character model pointer");
	require_text(owner.text, "player->haschrbody=false;",
		"rollback clears committed chrbody state");
}

TEST_CASE("chrRemove accepts model-less characters without weakening full teardown",
		"[chr][ownership][rollback][static][B-1067][T-ENGINE-004]")
{
	const std::string source = compact_source(
		read_pd_source("src/game/chr.c"));
	const source_block remove = definition_block(source,
		"voidchrRemove(structprop*prop,boolfree)");
	const source_block vertices_owned = block_at(remove.text,
		"if(model!=NULL)");
	const source_block model_owned = block_at(remove.text,
		"if(model!=NULL&&!corpseStoreOwnsModel(model))");

	require_text(vertices_owned.text,
		"modelFreeVertices(VTXSTORETYPE_CHRVTX,model);",
		"ordinary full-model teardown still frees character vertices");
	require_text(model_owned.text, "corpseStoreOwnsModel(model)",
		"ordinary full-model teardown preserves frozen-corpse ownership");
	require_text(model_owned.text, "modelmgrFreeModel(model);",
		"ordinary full-model teardown still releases unclaimed models");
	require_text(remove.text, "bgunFreeFireslotWrapper(chr->fireslots[0]);",
		"character teardown retains fireslot cleanup");
	require_text(remove.text, "propDeregisterRooms(prop);",
		"model-less and full-model characters both leave room registration");
	require_text(remove.text, "chr->model=NULL;",
		"character model ownership is cleared after optional model teardown");

	const size_t vertices = remove.text.find(
		"modelFreeVertices(VTXSTORETYPE_CHRVTX,model);");
	const size_t corpse = remove.text.find("corpseStoreOwnsModel(model)");
	const size_t free_model = remove.text.find("modelmgrFreeModel(model);");
	INFO("contract: every model consumer is protected by the explicit null guard");
	REQUIRE((vertices_owned.begin < vertices && vertices < vertices_owned.end));
	REQUIRE((model_owned.begin < corpse && corpse < model_owned.end));
	REQUIRE((model_owned.begin < free_model && free_model < model_owned.end));
}

TEST_CASE("smoke-only stage fault crosses the same reverse rollback boundary",
		"[player-init][stage-load][transaction][fault][static][B-1104][T-ENGINE-004]")
{
	const std::string lv_source = compact_source(
		read_pd_source("src/game/lv.c"));
	const std::string main_source = compact_source(
		read_pd_source("port/src/main.c"));
	const std::string fault_source = compact_source(
		read_pd_source("port/src/player_stage_init_fault.c"));
	const source_block lv_reset = definition_block(lv_source,
		"boollvReset(s32stagenum)");
	const source_block boot = definition_block(main_source,
		"staticvoidbootApplyCliFastPaths(void)");
	const source_block configure = definition_block(fault_source,
		"player_stage_init_fault_status_tplayerStageInitFaultConfigure("
		"constchar*spec,s32smoke_active,s32player_limit)");
	const source_block consume = definition_block(fault_source,
		"s32playerStageInitFaultPlanConsume(player_stage_init_fault_plan_t*plan,"
		"player_stage_init_fault_phase_tphase,s32playernum)");
	const source_block reset_fault = block_at(lv_reset.text,
		"if(playerStageInitFaultConsume(PLAYER_STAGE_INIT_FAULT_RESET,i))");
	const source_block spawn_fault = block_at(lv_reset.text,
		"if(playerStageInitFaultConsume(PLAYER_STAGE_INIT_FAULT_SPAWN,i))");

	require_text(boot.text,
		"playerStageInitFaultConfigure(player_init_fault,"
		"smokeHarnessIsActive()!=0,MAX_PLAYERS)",
		"ordinary binaries can arm the fault only through an active smoke");
	require_order(configure.text,
		{"playerStageInitFaultClear();", "if(!smoke_active)",
			"returnPLAYER_STAGE_INIT_FAULT_DISABLED;",
			"playerStageInitFaultPlanParse("},
		"non-smoke configuration clears prior state and rejects before parsing");
	require_text(consume.text,
		"plan->phase!=phase||plan->playernum!=playernum",
		"a mismatch cannot consume a later intended fault");
	require_order(consume.text,
		{"plan->consumed=1;", "plan->armed=0;", "return1;"},
		"an exact fault is a one-shot terminal transition");

	for (const source_block *fault : {&reset_fault, &spawn_fault}) {
		require_order(fault->text,
			{"PLAYER.INIT.FAULTconsumed",
				"lvRollbackCommittedPlayers(committed_player_order,",
				"modelmgrSetLvResetting(false);", "returnfalse;"},
			"injected failure uses production reverse rollback before stage abort");
	}
	INFO("contract: reset injection precedes per-player reset mutations");
	REQUIRE(reset_fault.begin < lv_reset.text.find(
		"g_Vars.currentplayer->usedowntime=0;"));
	INFO("contract: spawn injection precedes the selected playerSpawn call");
	REQUIRE(spawn_fault.end <= lv_reset.text.find(
		"enumplayer_chrbody_resultchrbody_result=playerSpawn();",
		spawn_fault.end));
}

TEST_CASE("host autostart selects network mission metadata through the ordinary lobby path",
		"[net][coop][counterop][lifecycle][static][B-1067][T-ENGINE-004]")
{
	const std::string main_source = compact_source(
		read_pd_source("port/src/main.c"));
	const std::string harness_source = compact_source(
		read_pd_source("port/src/smoke_harness.c"));
	const source_block tick = definition_block(main_source,
		"s32bootHostAutostartTick(void)");
	const source_block readiness = definition_block(harness_source,
		"staticsmoke_readiness_facts_tsmokeCaptureReadinessFacts(void)");

	require_text(tick.text, "constu8mode=g_BootHostAutostartGameMode;",
		"the one-shot carries an explicit frozen game mode");
	require_text(tick.text,
		"constu8difficulty=mode==NETGAMEMODE_MP?0:"
		"(u8)bootResolveDifficulty(g_BootLaunchDifficulty);",
		"network missions use the same bounded difficulty resolver as mission launch");
	require_text(tick.text,
		"if(mode==NETGAMEMODE_ANTI&&remoteParticipants!=1)",
		"Counter-Op refuses an ambiguous anti-player roster");
	require_text(tick.text, "antiClientId=client->id;",
		"Counter-Op identity comes from the exact joined room member");
	require_text(tick.text,
		"netLobbyRequestStartWithSims(mode,g_MatchConfig.stage_id,"
		"difficulty,antiClientId,",
		"all modes replay the ordinary high-level lobby start transaction");
	REQUIRE(tick.text.find("netServerCoopStageStart(") == std::string::npos);
	require_text(readiness.text,
		"facts.network_active&&(g_Vars.coopplayernum>=0||"
		"g_Vars.antiplayernum>=0)",
		"typed smoke readiness includes shipping co-op and Counter-Op roles");
}
