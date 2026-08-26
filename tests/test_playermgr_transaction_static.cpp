/*
 * Static source contract for T-ENGINE-004 player allocation.
 *
 * This test deliberately reads the canonical C sources.  A successful compile
 * alone does not prove that allocation is staged, published atomically, and
 * rolled back on every failure path.
 */

#include "catch.hpp"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <sstream>
#include <string>
#include <vector>

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

std::string compact_source(const std::string &text)
{
	std::string compact;
	compact.reserve(text.size());

	for (const unsigned char character : text) {
		if (!std::isspace(character)) {
			compact.push_back(static_cast<char>(character));
		}
	}

	return compact;
}

std::string read_pd_source(const char *relative_path)
{
	const std::filesystem::path path =
		std::filesystem::path(PD_SOURCE_DIR) / relative_path;
	INFO("PD_SOURCE_DIR: " << PD_SOURCE_DIR);
	INFO("source path: " << path.string());

	std::ifstream input(path, std::ios::in | std::ios::binary);
	REQUIRE(input.good());

	std::ostringstream output;
	output << input.rdbuf();
	return output.str();
}

void require_text(const std::string &text, const char *needle, const char *contract)
{
	INFO("contract: " << contract);
	INFO("missing source text: " << needle);
	REQUIRE(text.find(needle) != std::string::npos);
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
			INFO("previous marker position: " << previous);
			INFO("current marker position: " << current);
			REQUIRE(previous < current);
		}

		previous = current;
		first = false;
	}
}

source_block block_at(const std::string &text, const char *marker, size_t search_from = 0)
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

		/* Skip a forward declaration when the signature is also declared above
		 * the definition. */
		const size_t semicolon = text.find(';', begin);
		if (semicolon != std::string::npos && semicolon < open) {
			search_from = semicolon + 1;
			continue;
		}

		return block_at(text, signature, begin);
	}
}

size_t find_first_after(const std::string &text,
		std::initializer_list<const char *> needles,
		size_t after)
{
	size_t result = std::string::npos;

	for (const char *needle : needles) {
		const size_t position = text.find(needle, after);
		if (position != std::string::npos
				&& (result == std::string::npos || position < result)) {
			result = position;
		}
	}

	return result;
}

std::vector<size_t> player_publication_positions(const std::string &text)
{
	std::vector<size_t> positions;
	size_t search_from = 0;

	while (true) {
		const size_t player_reference = text.find("g_Vars.players[", search_from);
		if (player_reference == std::string::npos) {
			break;
		}

		const size_t semicolon = text.find(';', player_reference);
		const size_t equals = text.find('=', player_reference);
		if (semicolon != std::string::npos
				&& equals != std::string::npos
				&& equals < semicolon
				&& (equals + 1 >= text.size() || text[equals + 1] != '=')) {
			positions.push_back(player_reference);
		}

		search_from = player_reference + 1;
	}

	return positions;
}

} /* namespace */

TEST_CASE("playermgr allocation publishes only a completely staged player set",
		"[player-init][playermgr][transaction][static][T-ENGINE-004]")
{
	const std::string header = compact_source(
		read_pd_source("src/include/game/playermgr.h"));
	const std::string source = compact_source(
		read_pd_source("src/game/playermgr.c"));
	const std::string net_source = compact_source(
		read_pd_source("port/src/net/net.c"));

	const std::string enum_marker = "enumplayermgr_allocate_result{";
	require_text(header, enum_marker.c_str(),
			"playermgr allocation exposes a typed result enum");
	require_text(header, "PLAYMGR_ALLOC_OK=0",
			"typed allocation result includes OK");
	require_text(header, "PLAYMGR_ALLOC_INVALID_COUNT=-1",
			"typed allocation result includes INVALID_COUNT");
	require_text(header, "PLAYMGR_ALLOC_OUT_OF_MEMORY=-2",
			"typed allocation result includes OUT_OF_MEMORY");
	require_text(header, "PLAYMGR_ALLOC_NETWORK_REJECTED=-3",
			"typed allocation result includes NETWORK_REJECTED");
	require_text(header, "PLAYMGR_ALLOC_INVALID_ROLES=-4",
			"typed allocation result includes INVALID_ROLES");
	require_text(header,
			"enumplayermgr_allocate_resultplayermgrAllocatePlayers(s32count);",
			"allocation declaration returns the typed result");
	require_text(source,
			"enumplayermgr_allocate_resultplayermgrAllocatePlayers(s32count){",
			"allocation definition returns the typed enum");

	const source_block allocation = definition_block(
		source, "playermgrAllocatePlayers(s32count)");
	const source_block staging = block_at(
		allocation.text, "for(i=0;i<requested;i++)");
	const source_block commit = block_at(
		allocation.text, "for(i=0;i<requested;i++)", staging.end);

	require_text(allocation.text, "structplayer*staged[MAX_PLAYERS];",
			"allocation uses a local staged[MAX_PLAYERS] player-pointer array");
	require_text(staging.text, "mempAlloc(",
			"the first requested-player loop performs staging allocations");
	require_text(staging.text,
			"playermgrInitializePlayer(staged[i],i);",
			"each staged allocation is initialized before publication");

	INFO("contract: publication loop must follow the complete staging loop");
	REQUIRE(staging.end <= commit.begin);
	require_text(staging.text, "mempAlloc(",
			"staging loop contains allocation work");
	INFO("contract: staging loop must not publish g_Vars.players entries");
	REQUIRE(staging.text.find("g_Vars.players[") == std::string::npos);

	const std::vector<size_t> publications =
		player_publication_positions(allocation.text);
	INFO("contract: at least one staged player must be published at commit");
	REQUIRE(!publications.empty());

	bool found_staged_publication = false;
	for (const size_t publication : publications) {
		INFO("contract: every g_Vars.players publication follows staging");
		INFO("publication position: " << publication);
		REQUIRE(publication > staging.end);
		if (allocation.text.compare(publication,
				std::string("g_Vars.players[i]=staged[i]").size(),
				"g_Vars.players[i]=staged[i]") == 0) {
			found_staged_publication = true;
		}
	}
	REQUIRE(found_staged_publication);

	const std::string helper_signature =
		"staticvoidplayermgrInitializePlayer(structplayer*player,s32index)";
	const source_block helper = definition_block(source, helper_signature.c_str());
	const size_t helper_open = helper.text.find('{');
	REQUIRE(helper_open != std::string::npos);
	const std::string helper_body = helper.text.substr(helper_open + 1);

	require_text(helper_body, "player->",
			"private prepare helper initializes the supplied player pointer");
	INFO("contract: private prepare helper must not allocate another player");
	REQUIRE(helper_body.find("mempAlloc(") == std::string::npos);
	REQUIRE(helper_body.find("sizeof(structplayer)") == std::string::npos);

	require_text(commit.text, "g_Vars.players[i]=staged[i]",
			"commit loop publishes each completely prepared player");
	require_order(commit.text,
			{"g_Vars.players[i]=staged[i]", "playerResetCutsceneState(i)"},
			"cutscene reset occurs as part of commit after publication");
	INFO("contract: cutscene reset must not occur in private prepare");
	REQUIRE(helper_body.find("playerResetCutsceneState") == std::string::npos);

	const size_t allocation_publication =
		allocation.text.find("g_Vars.players[i]=staged[i]");
	REQUIRE(allocation_publication != std::string::npos);

	size_t allocation_count = 0;
	for (size_t search_from = 0;;) {
		const size_t allocation_call =
			allocation.text.find("mempAlloc(", search_from);
		if (allocation_call == std::string::npos) {
			break;
		}

		++allocation_count;
		const size_t null_check = find_first_after(
			allocation.text,
			{"if(!staged[i])", "if(staged[i]==NULL)", "if(staged[i]==0)"},
			allocation_call);
		INFO("contract: every mempAlloc is NULL-checked before publication");
		INFO("allocation occurrence: " << allocation_count);
		REQUIRE(null_check != std::string::npos);
		REQUIRE(allocation_call < null_check);
		REQUIRE(null_check < allocation_publication);
		search_from = allocation_call + 1;
	}
	INFO("contract: playermgr allocation must have a staged mempAlloc call");
	REQUIRE(allocation_count > 0);

	const source_block invalid_count = block_at(
		allocation.text, "if(count<0||requested>MAX_PLAYERS)");
	const source_block invalid_roles = block_at(
		allocation.text, "if(!playermgrRolesAreValid(requested))");
	const source_block out_of_memory = block_at(
		allocation.text, "if(!staged[i])");
	require_order(invalid_count.text,
			{"playermgrReset();", "PLAYER.INIT.ROLLBACK",
				"returnPLAYMGR_ALLOC_INVALID_COUNT;"},
			"invalid-count rollback resets, logs, and returns the typed failure");
	require_order(out_of_memory.text,
			{"playermgrReset();", "PLAYER.INIT.ROLLBACK",
				"returnPLAYMGR_ALLOC_OUT_OF_MEMORY;"},
			"allocation-failure rollback resets, logs, and returns the typed failure");
	require_order(invalid_roles.text,
			{"PLAYER.INIT.ROLLBACK", "returnPLAYMGR_ALLOC_INVALID_ROLES;"},
			"invalid roles reject before any private allocation or publication");
	REQUIRE(invalid_roles.begin < staging.begin);
	require_text(source, "g_Vars.coopplayernum>=0&&g_Vars.antiplayernum>=0",
		"role validation rejects simultaneous Co-Op and Counter-Op ownership");
	require_text(source,
		"g_Vars.coopplayernum==g_Vars.bondplayernum||g_Vars.antiplayernum==g_Vars.bondplayernum",
		"role validation rejects role aliasing with Bond");

	const size_t net_allocate = allocation.text.find(
		"netPlayersAllocate(staged,requested)");
	const size_t net_check = allocation.text.find(
		"if(net_result!=NET_PLAYER_ALLOC_OK)", net_allocate);
	const size_t network_rollback = allocation.text.find(
		"returnPLAYMGR_ALLOC_NETWORK_REJECTED;", net_check);
	const size_t commit_log = allocation.text.find(
		"PLAYER.INIT.COMMIT", network_rollback);
	INFO("contract: the overall commit log exists after network preflight");
	REQUIRE(commit_log != std::string::npos);
	INFO("contract: network allocation validates private candidates before local publication");
	REQUIRE(net_allocate != std::string::npos);
	REQUIRE(net_allocate < net_check);
	REQUIRE(net_check < network_rollback);
	REQUIRE(network_rollback < allocation_publication);
	REQUIRE(allocation_publication < commit_log);
	const std::string network_reject_marker =
		"returnPLAYMGR_ALLOC_NETWORK_REJECTED;";
	require_order(allocation.text.substr(net_check,
			network_rollback - net_check + network_reject_marker.size()),
		{"playermgrReset();", "globals_published=0",
			"returnPLAYMGR_ALLOC_NETWORK_REJECTED;"},
		"network rejection reports that no local player globals were published");

	const source_block net_allocation = definition_block(net_source,
		"netPlayersAllocate(structplayer*const*candidates,s32candidate_count)");
	require_text(net_allocation.text, "plan->player=candidates[playernum];",
		"network planning binds only the supplied private candidates");
	require_text(net_allocation.text, "if(plan_count!=candidate_count)",
		"network planning rejects a partial active-client roster");
	require_text(net_allocation.text, "NET_PLAYER_ALLOC_ROSTER_MISMATCH",
		"partial-roster rejection has a typed result");
	INFO("contract: network prepare must not consult globally published players");
	REQUIRE(net_allocation.text.find("g_Vars.players[") == std::string::npos);
}
