/*
 * tests/test_net_bot_pickup_static.cpp -- static guards for simulant pickups
 * and drops in networked play (wire v64, T-NETWORKING-011 finding 3).
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

	// Only a live simulant's pickup of a prop still in this world is replayed;
	// an item still respawning here is not refused (B-1205, next case)
	REQUIRE(read.find("chr->aibot == NULL") != std::string::npos);
	REQUIRE(read.find("prop->timetoregen != 0") == std::string::npos);
	REQUIRE(read.find("OBJHFLAG_DELETING") != std::string::npos);
}

TEST_CASE("bot pickup net: a client finishes an item's respawn before it replays the host's pickup of it",
          "[net][botpickup][b1205]") {
	// B-1205: each machine starts an item's respawn wait when it learns of the
	// pickup before, so a client's wait can end a few frames after the host's.
	// A simulant waiting on the spot took the shield the moment the host's
	// copy was back, and the client skipped the replay ("waiting to respawn").
	const std::string propobj = code_only(read_text_file("src/game/propobj.c"));
	const std::string netmsg = code_only(read_text_file("port/src/net/netmsg.c"));

	// objTick's respawn steps live in one helper, which objTick runs with the
	// frame's time and objRegenNow runs to the end of the wait
	const std::string regen = function_body(propobj,
		"static void objTickRegen(struct prop *prop, s32 elapsed60)\n{");
	REQUIRE(regen.find("prop->timetoregen -= elapsed60;") != std::string::npos);
	REQUIRE(regen.find("propEnable(prop);") != std::string::npos);
	REQUIRE(regen.find("shield->amount = shield->initialamount;") != std::string::npos);
	REQUIRE(regen.find("SFX_REGEN") != std::string::npos);
	REQUIRE(regen.find("g_Vars.lvupdate60") == std::string::npos);

	const std::string tick = function_body(propobj, "u32 objTick(struct prop *prop)\n{");
	REQUIRE(tick.find("objTickRegen(prop, g_Vars.lvupdate60);") != std::string::npos);
	REQUIRE(tick.find("prop->timetoregen -=") == std::string::npos);

	const std::string now = function_body(propobj, "void objRegenNow(struct prop *prop)\n{");
	const size_t fadein = now.find("objTickRegen(prop, prop->timetoregen - (TICKS(60) - 1));");
	const size_t finish = now.find("objTickRegen(prop, prop->timetoregen);");
	REQUIRE(fadein != std::string::npos);
	REQUIRE(finish != std::string::npos);
	REQUIRE(fadein < finish);

	// Both pickup replays finish the respawn before they take the item
	const std::string botread = function_body(netmsg,
		"u32 netmsgSvcBotPickupRead(struct netbuf *src, struct netclient *srccl)\n{");
	const size_t botregen = botread.find("objRegenNow(prop);");
	REQUIRE(botregen != std::string::npos);
	REQUIRE(botregen < botread.find("botPickupProp(prop, chr)"));

	const std::string humanread = function_body(netmsg,
		"u32 netmsgSvcPropPickupRead(struct netbuf *src, struct netclient *srccl)\n{");
	const size_t humanregen = humanread.find("objRegenNow(prop);");
	REQUIRE(humanregen != std::string::npos);
	REQUIRE(humanregen < humanread.find("propPickupByPlayer(prop, true);"));
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
	// B-1194: a weapon made for a chr after the stage start has a dynamic sync
	// id only the host knows; the host notes every such weapon the chr carries
	// before its drop and announces each one the drop puts in the world, and a
	// client's puppet retires its own unnumbered copy
	const std::string chr = code_only(read_text_file("src/game/chr.c"));
	const size_t drop = chr.find("objDropRecursively(prop, false);");
	REQUIRE(drop != std::string::npos);
	const size_t carried = chr.rfind("for (struct prop *carried = prop->child; carried; carried = carried->next) {", drop);
	const size_t noted = chr.find("dropweapons[numdropweapons++] = carried;", carried);
	REQUIRE(carried != std::string::npos);
	REQUIRE(noted < drop);
	// botDisarm clears weapons_held before this block runs, so the note must
	// come from the carried props, not the held slots
	REQUIRE(chr.substr(carried, drop - carried).find("weapons_held") == std::string::npos);
	const size_t server = chr.find("if (g_NetMode == NETMODE_SERVER) {", drop);
	const size_t parent = chr.find("wprop->parent == NULL", drop);
	const size_t dynamic = chr.find("wprop->syncid >= g_NetFirstDynamicSyncId", drop);
	const size_t spawn = chr.find("netmsgSvcPropSpawnWrite(&g_NetMsgRel, wprop) == 0", drop);
	const size_t move = chr.find("netmsgSvcPropMoveWrite(&g_NetMsgRel, wprop, NULL);", drop);
	REQUIRE(server != std::string::npos);
	REQUIRE(parent != std::string::npos);
	REQUIRE(dynamic != std::string::npos);
	REQUIRE(spawn != std::string::npos);
	REQUIRE(move != std::string::npos);
	REQUIRE(server < parent);
	REQUIRE(parent < dynamic);
	REQUIRE(dynamic < spawn);
	REQUIRE(spawn < move);

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

TEST_CASE("bot pickup net: a disarmed simulant's gun is the host's to drop",
          "[net][botpickup][b1194]") {
	// B-1194: SVC_CHR_DAMAGE runs the host's disarm on a client's puppet too;
	// the puppet retires its held model and drops or makes nothing, so the
	// only gun in the world is the host's announced one
	const std::string bot = code_only(read_text_file("src/game/bot.c"));
	const std::string disarm = function_body(bot,
		"void botDisarm(struct chrdata *chr, struct prop *attackerprop)\n{");
	const size_t puppet = disarm.find("if (g_NetMode == NETMODE_CLIENT && !g_NetLocalBotAuthority) {");
	const size_t retire = disarm.find("chr->weapons_held[HAND_RIGHT]->obj->hidden |= OBJHFLAG_DELETING;", puppet);
	const size_t host = disarm.find("} else if (chr->weapons_held[HAND_RIGHT]) {", retire);
	const size_t make = disarm.find("weaponCreateForChr(", host);
	const size_t dropped = disarm.find("objSetDropped(prop, DROPTYPE_DEFAULT);", make);
	REQUIRE(puppet != std::string::npos);
	REQUIRE(retire != std::string::npos);
	REQUIRE(host != std::string::npos);
	REQUIRE(make != std::string::npos);
	REQUIRE(dropped != std::string::npos);
	REQUIRE(puppet < retire);
	REQUIRE(retire < host);
	// the puppet branch leaves prop NULL, so the shared drop below skips it
	REQUIRE(disarm.substr(puppet, host - puppet).find("prop =") == std::string::npos);
}

TEST_CASE("bot tick: a simulant's update runs once a frame however many players the frame passes",
          "[net][botpickup][b1202]") {
	// B-1202: lvRender runs propsTickPlayer once for each player of a
	// networked match; the frame's first pass marks every prop not yet
	// ticked, and botTick must read that mark before its chrTick clears it,
	// or every later pass runs the simulant's AI, scenario points and pickups
	// again
	const std::string lv = code_only(read_text_file("src/game/lv.c"));
	const size_t force = lv.find("forcesingleplayer = false;");
	REQUIRE(force != std::string::npos);
	const size_t net = lv.rfind("if (g_NetMode) {", force);
	REQUIRE(net != std::string::npos);
	REQUIRE(lv.substr(net, force - net).find(';') == std::string::npos);
	const size_t count = lv.find("playercount = forcesingleplayer ? 1 : PLAYERCOUNT();", force);
	REQUIRE(count != std::string::npos);
	REQUIRE(lv.find("propsTickPlayer(islastplayer);", count) != std::string::npos);

	const std::string prop = code_only(read_text_file("src/game/prop.c"));
	const size_t first = prop.find("if (g_Vars.currentplayerindex == 0) {");
	REQUIRE(first != std::string::npos);
	REQUIRE(prop.find("flags |= PROPFLAG_NOTYETTICKED;", first) < prop.find("} else {", first));

	const std::string chr = code_only(read_text_file("src/game/chr.c"));
	REQUIRE(chr.find("prop->flags &= ~PROPFLAG_NOTYETTICKED;") != std::string::npos);

	const std::string bot = code_only(read_text_file("src/game/bot.c"));
	const std::string tick = function_body(bot, "s32 botTick(struct prop *prop)\n{");
	const size_t gate = tick.find("updateable = g_Vars.lvupdate240 != 0 && (prop->flags & PROPFLAG_NOTYETTICKED) != 0;");
	REQUIRE(gate != std::string::npos);
	REQUIRE(gate < tick.find("chrTick(prop)"));
	REQUIRE(tick.find("updateable = g_Vars.lvupdate240 != 0;") == std::string::npos);
	REQUIRE(tick.find("updateable = ", gate + 1) == std::string::npos);
	// the AI step, the scenario points and the pickups all wait on the gate
	REQUIRE(tick.find("if (updateable && g_Vars.lvframe60 >= 145) {") != std::string::npos);
	const size_t scenario = tick.find("scenarioTickChr(chr);");
	REQUIRE(scenario != std::string::npos);
	REQUIRE(tick.rfind("if (updateable) {", scenario) != std::string::npos);
	REQUIRE(tick.find("if (updateable && !chrIsDead(chr)) {", scenario) < tick.find("botCheckPickups(chr);", scenario));
}

TEST_CASE("prop scheduler: a networked match counts the frame's time once however many players it passes",
          "[net][botpickup][b1202][sp87]") {
	// SP-87 (CORE agreed 2026-11-16): a networked match passes the props once
	// per player each frame; only the frame's first pass adds the frame's time
	// to the slots and props and advances the background slot, and a later
	// pass ticks no background prop. Local split screen keeps the original
	// per-pass accounting, so the gate needs a networked mode
	const std::string prop = code_only(read_text_file("src/game/prop.c"));
	const std::string tick = function_body(prop, "void propsTickPlayer(bool islastplayer)\n{");
	const size_t gate = tick.find("netrepeatpass = g_NetMode != NETMODE_NONE && g_Vars.currentplayerindex != 0;");
	REQUIRE(gate != std::string::npos);

	const size_t slots = tick.find("if (!netrepeatpass) {", gate);
	REQUIRE(slots != std::string::npos);
	const size_t slottime = tick.find("g_Vars.propstates[i].slotupdate240 += g_Vars.lvupdate240;", slots);
	const size_t advance = tick.find("g_Vars.runstateindex++;", slots);
	REQUIRE(slottime != std::string::npos);
	REQUIRE(advance != std::string::npos);
	// both inside the first-pass block, before the loop takes runstateindex
	const size_t take = tick.find("runstateindex = g_Vars.runstateindex;", slots);
	REQUIRE(take != std::string::npos);
	REQUIRE(slottime < take);
	REQUIRE(advance < take);
	// the gated ones are the only slot accumulation and slot advance
	REQUIRE(tick.find("slotupdate240 += ") == tick.rfind("slotupdate240 += "));
	REQUIRE(tick.find("runstateindex++") == tick.rfind("runstateindex++"));
	REQUIRE(tick.find("g_Vars.runstateindex++;") == advance);

	const size_t proptime = tick.find("prop->propupdate240 += g_Vars.lvupdate240;");
	REQUIRE(proptime != std::string::npos);
	REQUIRE(tick.rfind("if (!netrepeatpass) {", proptime) > take);
	REQUIRE(tick.find("prop->propupdate240 += g_Vars.lvupdate240;", proptime + 1) == std::string::npos);

	REQUIRE(tick.find("if (prop->propstateindex == runstateindex && !netrepeatpass) {") != std::string::npos);
	REQUIRE(tick.find("if (prop->propstateindex == runstateindex) {") == std::string::npos);
}

TEST_CASE("bot pickup net: a scenario prop the host makes during the match is announced",
          "[net][scenario][b1209]") {
	// B-1209 (SP-85): Hacker Central makes a new uplink each time its carrier
	// dies, and Hold the Briefcase a new case when none is left. A client never
	// makes its own, so before the announcement it never saw them and skipped
	// every host pickup of them.
	const std::string scenarios = code_only(read_text_file("src/game/mplayer/scenarios.c"));
	const std::string announce = function_body(scenarios,
		"void scenarioNetAnnounceProp(struct prop *prop, const char *what)\n{");
	const size_t gate = announce.find("if (g_NetMode != NETMODE_SERVER || prop == NULL) {");
	const size_t spawn = announce.find("netmsgSvcPropSpawnWrite(&g_NetMsgRel, prop);");
	const size_t move = announce.find("netmsgSvcPropMoveWrite(&g_NetMsgRel, prop, NULL);");
	REQUIRE(gate != std::string::npos);
	REQUIRE(spawn != std::string::npos);
	REQUIRE(move != std::string::npos);
	REQUIRE(gate < spawn);
	REQUIRE(spawn < move);

	struct site { const char *file; const char *tick; const char *create; const char *told; };
	const site sites[] = {
		{ "src/game/mplayer/scenarios/hackthatmac.inc", "void htmTick(void)\n{", "htbCreateUplink();",
			"scenarioNetAnnounceProp(g_ScenarioData.htm.uplink, \"uplink\");" },
		{ "src/game/mplayer/scenarios/holdthebriefcase.inc", "void htbTick(void)\n{", "htbCreateToken();",
			"scenarioNetAnnounceProp(g_ScenarioData.htb.token, \"briefcase\");" },
	};

	for (const site &s : sites) {
		INFO(s.file);
		const std::string tick = function_body(code_only(read_text_file(s.file)), s.tick);
		const size_t client = tick.find("if (!scenarioNetIsAuthority()) {");
		const size_t create = tick.find(s.create);
		const size_t told = tick.find(s.told);
		REQUIRE(client != std::string::npos);
		REQUIRE(create != std::string::npos);
		REQUIRE(told != std::string::npos);
		// only the host makes it during the match, and tells the clients at once
		REQUIRE(client < create);
		REQUIRE(create < told);
		REQUIRE(tick.find(s.create, create + 1) == std::string::npos);
	}
}

TEST_CASE("bot pickup net: the match manifest lists the scenario's token weapon",
          "[net][scenario][b1272]") {
	// B-1272 (SP-93): the data uplink and the briefcase have no MP weapon slot,
	// so the manifest's weapon pool left them out of the session catalog, and
	// every message naming one (B-1209's replacement uplink, a carrier's drop,
	// a simulant's held weapon) reached a client as WEAPON_UNARMED: in
	// mp_hacker_central_net_carrier_death_smoke the host sent "item 48
	// weapon=46" and the client replayed "item 48 weapon=1".
	const std::string scenarios = code_only(read_text_file("src/game/mplayer/scenarios.c"));
	const std::string token = function_body(scenarios, "s32 scenarioTokenWeaponNum(s32 scenario)\n{");
	REQUIRE(token.find("case MPSCENARIO_HACKERCENTRAL:\n\t\treturn WEAPON_DATAUPLINK;") != std::string::npos);
	REQUIRE(token.find("case MPSCENARIO_HOLDTHEBRIEFCASE:\n\tcase MPSCENARIO_CAPTURETHECASE:\n\t\treturn WEAPON_BRIEFCASE2;")
		!= std::string::npos);
	REQUIRE(token.find("default:\n\t\treturn -1;") != std::string::npos);

	const std::string netmsg = code_only(read_text_file("port/src/net/netmsg.c"));
	const std::string prepare = function_body(netmsg,
		"static bool netLobbyStartWritePrepare(net_lobby_start_write_plan_t *plan,");
	const size_t mp = prepare.find("if (gamemode == NETGAMEMODE_MP) {");
	const size_t scenario = prepare.find("plan->scenario_id, MANIFEST_SLOT_MATCH)) {", mp);
	const size_t lookup = prepare.find("const s32 token = scenarioTokenWeaponNum((s32)plan->scenario);");
	const size_t append = prepare.find("if (!token_id || !manifestAppendCatalogClosure(&plan->manifest,\n"
		"\t\t\t\t\t\ttoken_id, MANIFEST_SLOT_MATCH)) {");
	const size_t reject = prepare.find("\"host manifest cannot publish the scenario's token weapon\"");
	const size_t hash = prepare.find("manifestComputeHash(&plan->manifest);");
	REQUIRE(mp != std::string::npos);
	REQUIRE(scenario != std::string::npos);
	REQUIRE(lookup != std::string::npos);
	REQUIRE(append != std::string::npos);
	REQUIRE(reject != std::string::npos);
	REQUIRE(hash != std::string::npos);
	// in the Combat Simulator branch, after the scenario's own closure and
	// before the manifest is hashed and published
	REQUIRE(mp < scenario);
	REQUIRE(scenario < lookup);
	REQUIRE(lookup < append);
	REQUIRE(append < reject);
	REQUIRE(reject < hash);

	// a real weapon with no session id is reported, once per weapon
	const std::string writer = function_body(netmsg,
		"static void netWriteWeaponRef(struct netbuf *dst, s32 weaponnum)\n{");
	REQUIRE(writer.find("if (session == 0 && weaponnum > WEAPON_UNARMED && weaponnum < WEAPON_CUSTOM_END\n"
		"\t\t\t&& sessionCatalogIsActive()) {") != std::string::npos);
	REQUIRE(writer.find("has no session catalog id; clients read it as unarmed") != std::string::npos);
	REQUIRE(writer.find("catalogWriteAssetRef(dst, session);") != std::string::npos);

	// and a model with no session id (a reader turns it into model 0)
	const std::string model_writer = function_body(netmsg,
		"static void netWriteModelRef(struct netbuf *dst, s32 modelnum)\n{");
	REQUIRE(model_writer.find("if (session == 0 && modelnum > 0 && modelnum < MODEL_CUSTOM_END\n"
		"\t\t\t&& sessionCatalogIsActive()) {") != std::string::npos);
	REQUIRE(model_writer.find("has no session catalog id; clients read it as model 0") != std::string::npos);
	REQUIRE(model_writer.find("catalogWriteAssetRef(dst, session);") != std::string::npos);
}

TEST_CASE("bot stuck watchdog: a simulant downloading at a Hacker Central terminal is not stuck",
          "[bot][scenario][b1207]") {
	// B-1207: a download is 20 s standing at the terminal; the port's stuck
	// watchdog counted MA_AIBOTDOWNLOAD as navigation intent and moved the
	// simulant away about 3 s into every download, so none ever completed.
	const std::string bot = code_only(read_text_file("src/game/bot.c"));
	const size_t intent = bot.find("bool hasIntent = (chr->myaction == MA_AIBOTMAINLOOP");
	REQUIRE(intent != std::string::npos);
	REQUIRE(bot.find("bool hasIntent = ", intent + 1) == std::string::npos);
	const size_t end = bot.find(");", intent);
	REQUIRE(end != std::string::npos);
	const std::string list = bot.substr(intent, end - intent);
	REQUIRE(list.find("MA_AIBOTGOTOPROP") != std::string::npos);
	REQUIRE(list.find("MA_AIBOTDOWNLOAD") == std::string::npos);
	// the relocation is still gated on that intent
	const size_t gated = bot.find("} else if (hasIntent) {", end);
	const size_t relocate = bot.find("relocated to safe waypoint", end);
	REQUIRE(gated != std::string::npos);
	REQUIRE(relocate != std::string::npos);
	REQUIRE(gated < relocate);
}
