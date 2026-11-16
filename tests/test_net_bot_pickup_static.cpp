/*
 * tests/test_net_bot_pickup_static.cpp -- static guards for simulant pickups
 * and drops in networked play (wire v62, T-NETWORKING-011 finding 3).
 *
 * Simulant AI runs only on the host. Before these paths, a client kept every
 * prop a simulant picked up lying where it was, and made its own copies of
 * what a dead simulant dropped (sync id 0), which no host pickup message
 * could reach. The paths are too coupled to the game loop for pd-tests to
 * run, so these guards pin their shape in the source: the host announces each
 * simulant pickup that took effect (SVC_BOT_PICKUP) and each simulant drop
 * (SVC_PROP_SPAWN), a client never makes drop props of its own, a client's
 * skip of a pickup it cannot apply does not stop the rest of the packet, and
 * a human's Capture the Case capture reaches the clients too.
 *
 * Pins are matched against the code with comments blanked out, so prose that
 * names a function cannot satisfy or break them.
 */

#include "catch.hpp"

#include <fstream>
#include <regex>
#include <sstream>
#include <string>

namespace {

std::string read_text_file(const char *path)
{
	std::ifstream in(path, std::ios::in | std::ios::binary);
	INFO("source file " << path);
	REQUIRE(in.good());
	std::ostringstream ss;
	ss << in.rdbuf();
	return ss.str();
}

/* The text with every comment replaced by spaces (newlines kept), so offsets
 * and line structure stay as they were. */
std::string code_only(const std::string &text)
{
	enum class lex { normal, line_comment, block_comment, string_literal, char_literal };
	std::string out = text;
	lex state = lex::normal;

	for (size_t pos = 0; pos < text.size(); pos++) {
		const char ch = text[pos];
		const char next = pos + 1 < text.size() ? text[pos + 1] : '\0';

		switch (state) {
		case lex::normal:
			if (ch == '/' && next == '/') {
				state = lex::line_comment;
				out[pos] = ' ';
				out[pos + 1] = ' ';
				pos++;
			} else if (ch == '/' && next == '*') {
				state = lex::block_comment;
				out[pos] = ' ';
				out[pos + 1] = ' ';
				pos++;
			} else if (ch == '"') {
				state = lex::string_literal;
			} else if (ch == '\'') {
				state = lex::char_literal;
			}
			break;
		case lex::line_comment:
			if (ch == '\n') {
				state = lex::normal;
			} else {
				out[pos] = ' ';
			}
			break;
		case lex::block_comment:
			if (ch == '*' && next == '/') {
				out[pos] = ' ';
				out[pos + 1] = ' ';
				pos++;
				state = lex::normal;
			} else if (ch != '\n') {
				out[pos] = ' ';
			}
			break;
		case lex::string_literal:
		case lex::char_literal:
			if (ch == '\\' && pos + 1 < text.size()) {
				pos++;
			} else if ((state == lex::string_literal && ch == '"') || (state == lex::char_literal && ch == '\'')) {
				state = lex::normal;
			}
			break;
		}
	}

	return out;
}

/* The body of the function whose definition starts with signature, braces
 * included. Strings are skipped while matching braces. */
std::string function_body(const std::string &code, const std::string &signature)
{
	const size_t begin = code.find(signature);
	INFO("function " << signature);
	REQUIRE(begin != std::string::npos);

	const size_t brace = code.find('{', begin);
	REQUIRE(brace != std::string::npos);

	size_t depth = 0;
	bool in_string = false;
	bool in_char = false;

	for (size_t pos = brace; pos < code.size(); pos++) {
		const char ch = code[pos];

		if (in_string || in_char) {
			if (ch == '\\') {
				pos++;
			} else if ((in_string && ch == '"') || (in_char && ch == '\'')) {
				in_string = false;
				in_char = false;
			}
			continue;
		}

		if (ch == '"') {
			in_string = true;
		} else if (ch == '\'') {
			in_char = true;
		} else if (ch == '{') {
			depth++;
		} else if (ch == '}') {
			depth--;
			if (depth == 0) {
				return code.substr(brace, pos - brace + 1);
			}
		}
	}

	FAIL("function body not closed: " << signature);
	return std::string();
}

size_t count_of(const std::string &text, const std::string &needle)
{
	size_t count = 0;

	for (size_t pos = text.find(needle); pos != std::string::npos; pos = text.find(needle, pos + needle.size())) {
		count++;
	}

	return count;
}

} // namespace

TEST_CASE("bot pickup net: SVC_BOT_PICKUP has a message id of its own",
          "[net][botpickup][finding3]") {
	const std::string header = code_only(read_text_file("port/include/net/netmsg.h"));
	const std::regex define_4d(R"(#define[ \t]+SVC_[A-Z0-9_]+[ \t]+0x4[dD]\b)");

	REQUIRE(header.find("#define SVC_BOT_PICKUP    0x4D") != std::string::npos);
	REQUIRE(std::distance(std::sregex_iterator(header.begin(), header.end(), define_4d), std::sregex_iterator()) == 1);
}

TEST_CASE("bot pickup net: a networked client dispatches SVC_BOT_PICKUP",
          "[net][botpickup][finding3]") {
	const std::string net = code_only(read_text_file("port/src/net/net.c"));
	const std::string receive = function_body(net, "static void netClientEvReceive(struct netclient *cl)");

	REQUIRE(receive.find("case SVC_BOT_PICKUP: rc = netmsgSvcBotPickupRead(&cl->in, cl); break;") != std::string::npos);
}

TEST_CASE("bot pickup net: a client replays the pickup and never stops the packet over a skip",
          "[net][botpickup][finding3]") {
	const std::string netmsg = code_only(read_text_file("port/src/net/netmsg.c"));
	const std::string read = function_body(netmsg,
		"u32 netmsgSvcBotPickupRead(struct netbuf *src, struct netclient *srccl)\n{");

	// Malformed bytes stop the packet; a pickup this client cannot apply does not
	REQUIRE(read.find("return src->error;") != std::string::npos);
	REQUIRE(read.find("return 1") == std::string::npos);
	REQUIRE(read.find("src->error ? src->error : 1") == std::string::npos);

	// The same pickup as the host's, then the same tick operation botCheckPickups runs
	const size_t pickup = read.find("botPickupProp(prop, chr)");
	const size_t tickop = read.find("propExecuteTickOperation(prop, TICKOP_FREE)");
	REQUIRE(pickup != std::string::npos);
	REQUIRE(tickop != std::string::npos);
	REQUIRE(pickup < tickop);

	// Only a live simulant's pickup of a prop still in this world is replayed
	REQUIRE(read.find("chr->aibot == NULL") != std::string::npos);
	REQUIRE(read.find("prop->timetoregen != 0") != std::string::npos);
	REQUIRE(read.find("OBJHFLAG_DELETING") != std::string::npos);
}

TEST_CASE("bot pickup net: the host announces each simulant pickup that took effect",
          "[net][botpickup][finding3]") {
	const std::string bot = code_only(read_text_file("src/game/bot.c"));
	const std::string test = function_body(bot, "bool botTestPropForPickup(struct prop *prop, struct chrdata *chr)\n{");
	const std::string announce = function_body(bot,
		"static u32 botPickupPropAndAnnounce(struct prop *prop, struct chrdata *chr)\n{");

	// Every pickup a simulant makes goes through the announcing wrapper
	REQUIRE(count_of(test, "botPickupPropAndAnnounce(prop, chr)") == 1);
	REQUIRE(test.find("return botPickupProp(") == std::string::npos);

	// The sync id is taken before the pickup, which may free the prop
	const size_t syncid = announce.find("prop->syncid");
	const size_t pickup = announce.find("botPickupProp(prop, chr)");
	const size_t send = announce.find("netmsgSvcBotPickupWrite(&g_NetMsgRel, chr, syncid)");
	REQUIRE(syncid != std::string::npos);
	REQUIRE(pickup != std::string::npos);
	REQUIRE(send != std::string::npos);
	REQUIRE(syncid < pickup);
	REQUIRE(pickup < send);
	REQUIRE(announce.find("g_NetMode == NETMODE_SERVER") != std::string::npos);

	// Scenario token pickups return TICKOP_NONE: they count by the deletion
	// mark, and a Capture the Case capture by the carried case
	REQUIRE(announce.find("OBJHFLAG_DELETING") != std::string::npos);
	REQUIRE(announce.find("hascase") != std::string::npos);
}

TEST_CASE("bot pickup net: only the host makes and announces what a simulant drops",
          "[net][botpickup][finding3]") {
	const std::string botinv = code_only(read_text_file("src/game/botinv.c"));
	const std::string drop = function_body(botinv, "void botinvDrop(struct chrdata *chr, s32 weaponnum, u8 dropall)\n{");

	const size_t clientguard = drop.find("g_NetMode != NETMODE_CLIENT");
	const size_t create = drop.find("weaponCreateForChr(");
	const size_t token = drop.find("scenarioHandleDroppedToken(chr, prop)");
	const size_t spawn = drop.find("netmsgSvcPropSpawnWrite(&g_NetMsgRel, prop)");
	const size_t move = drop.find("netmsgSvcPropMoveWrite(&g_NetMsgRel, prop, NULL)");

	REQUIRE(clientguard != std::string::npos);
	REQUIRE(create != std::string::npos);
	REQUIRE(clientguard < create);
	REQUIRE(count_of(drop, "weaponCreateForChr(") == 1);

	// Announced after a Capture the Case case went back to its base, so the
	// spawn carries the final position
	REQUIRE(token != std::string::npos);
	REQUIRE(spawn != std::string::npos);
	REQUIRE(move != std::string::npos);
	REQUIRE(token < spawn);
	REQUIRE(spawn < move);

	// A half-dropped prop is retired, not announced
	REQUIRE(drop.find("!objDrop(prop, true)") != std::string::npos);
}

TEST_CASE("bot pickup net: a human's Capture the Case capture reaches the clients",
          "[net][botpickup][finding3]") {
	const std::string propobj = code_only(read_text_file("src/game/propobj.c"));

	const size_t hadcase = propobj.find("const s32 hadcase = g_MpSetup.scenario == MPSCENARIO_CAPTURETHECASE && invHasBriefcase();");
	const size_t capture = propobj.find("hadcase && !invHasBriefcase()");
	const size_t send = propobj.find("netmsgSvcPropPickupWrite(&g_NetMsgRel, g_Vars.currentplayer->client, prop, TICKOP_NONE)");

	REQUIRE(hadcase != std::string::npos);
	REQUIRE(capture != std::string::npos);
	REQUIRE(send != std::string::npos);
	REQUIRE(hadcase < capture);
	REQUIRE(capture < send);
}

TEST_CASE("bot pickup net: client token searches skip a copy marked for deletion",
          "[net][botpickup][finding3]") {
	const char *files[] = {
		"src/game/mplayer/scenarios/holdthebriefcase.inc",
		"src/game/mplayer/scenarios/hackthatmac.inc",
		"src/game/mplayer/scenarios/capturethecase.inc",
	};
	const char *presenters[] = {
		"static void htbNetPresent(void)\n{",
		"static void htmNetPresent(void)\n{",
		"static void ctcNetPresent(void)\n{",
	};

	for (size_t i = 0; i < 3; i++) {
		const std::string code = code_only(read_text_file(files[i]));
		const std::string present = function_body(code, presenters[i]);

		INFO("presenter " << presenters[i]);
		REQUIRE(present.find("(prop->obj->hidden & OBJHFLAG_DELETING) == 0") != std::string::npos);
	}
}

TEST_CASE("bot pickup net: a simulant's held weapons dropped at its death carry the host's sync ids",
          "[net][botpickup][b1194]") {
	// B-1194: a held weapon equipped after the stage start has a dynamic sync
	// id only the host knows; the host announces it once its death drop puts
	// it in the world, and a client's puppet retires its own unnumbered copy
	const std::string chr = code_only(read_text_file("src/game/chr.c"));
	const size_t drop = chr.find("objDropRecursively(prop, false);");
	REQUIRE(drop != std::string::npos);
	const size_t server = chr.find("if (g_NetMode == NETMODE_SERVER) {", drop);
	const size_t dynamic = chr.find("wprop->syncid >= g_NetFirstDynamicSyncId", drop);
	const size_t spawn = chr.find("netmsgSvcPropSpawnWrite(&g_NetMsgRel, wprop) == 0", drop);
	const size_t move = chr.find("netmsgSvcPropMoveWrite(&g_NetMsgRel, wprop, NULL);", drop);
	REQUIRE(server != std::string::npos);
	REQUIRE(dynamic != std::string::npos);
	REQUIRE(spawn != std::string::npos);
	REQUIRE(move != std::string::npos);
	REQUIRE(server < dynamic);
	REQUIRE(dynamic < spawn);
	REQUIRE(spawn < move);
	REQUIRE(chr.find("struct prop *heldprops[2] = { chr->weapons_held[0], chr->weapons_held[1] };") < drop);

	const std::string action = code_only(read_text_file("src/game/chraction.c"));
	const std::string death = function_body(action,
		"void chrBeginDeath(struct chrdata *chr, struct coord *dir, f32 relangle, s32 hitpart, struct gset *gset, bool knockout, s32 aplayernum)\n{");
	const size_t puppet = death.find("if (chr->aibot && g_NetMode == NETMODE_CLIENT && !g_NetLocalBotAuthority) {");
	const size_t retire = death.find("chr->weapons_held[i]->obj->hidden |= OBJHFLAG_DELETING;", puppet);
	const size_t local = death.find("objSetDropped(chr->weapons_held[0], DROPTYPE_DEFAULT);");
	REQUIRE(puppet != std::string::npos);
	REQUIRE(retire != std::string::npos);
	REQUIRE(local != std::string::npos);
	REQUIRE(puppet < retire);
	REQUIRE(retire < local);
	REQUIRE(death.find("} else {", retire) < local);
}
