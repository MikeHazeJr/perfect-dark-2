/*
 * tests/test_net_reader_consumes_static.cpp -- static guards for B-1131 /
 * SP-81: a client read handler reads its whole message before it decides,
 * and declines a well-formed message by returning 0.
 *
 * The client receive loop (netClientEvReceive) reads messages while the
 * handler result is 0. A handler that returned 0 with part of its message
 * unread made the loop read that tail as the next messages (a desync); one
 * that returned nonzero for an ordinary miss (a prop or character this client
 * does not have) dropped the rest of the packet. These readers need the live
 * prop and client tables, so the guards pin their shape in the source
 * (comments blanked out).
 */

#include "catch.hpp"

#include <fstream>
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

/* The text with comments replaced by spaces; strings are left alone. */
std::string code_only(const std::string &text)
{
	std::string out = text;
	bool line = false;
	bool block = false;
	bool str = false;

	for (size_t pos = 0; pos < text.size(); pos++) {
		const char ch = text[pos];
		const char next = pos + 1 < text.size() ? text[pos + 1] : '\0';

		if (line) {
			if (ch == '\n') {
				line = false;
			} else {
				out[pos] = ' ';
			}
		} else if (block) {
			if (ch == '*' && next == '/') {
				out[pos] = ' ';
				out[pos + 1] = ' ';
				pos++;
				block = false;
			} else if (ch != '\n') {
				out[pos] = ' ';
			}
		} else if (str) {
			if (ch == '\\') {
				pos++;
			} else if (ch == '"') {
				str = false;
			}
		} else if (ch == '/' && next == '/') {
			out[pos] = ' ';
			out[pos + 1] = ' ';
			pos++;
			line = true;
		} else if (ch == '/' && next == '*') {
			out[pos] = ' ';
			out[pos + 1] = ' ';
			pos++;
			block = true;
		} else if (ch == '"') {
			str = true;
		}
	}

	return out;
}

std::string function_body(const std::string &code, const std::string &signature)
{
	const size_t begin = code.find(signature);
	INFO("function " << signature);
	REQUIRE(begin != std::string::npos);

	const size_t brace = code.find('{', begin);
	REQUIRE(brace != std::string::npos);

	size_t depth = 0;
	bool str = false;

	for (size_t pos = brace; pos < code.size(); pos++) {
		const char ch = code[pos];

		if (str) {
			if (ch == '\\') {
				pos++;
			} else if (ch == '"') {
				str = false;
			}
			continue;
		}

		if (ch == '"') {
			str = true;
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

const std::string &netmsg_code()
{
	static const std::string code = code_only(read_text_file("port/src/net/netmsg.c"));
	return code;
}

} // namespace

TEST_CASE("net readers: SVC_PLAYER_STATS reads its ammo tail before it decides",
          "[net][b1131]") {
	const std::string body = function_body(netmsg_code(),
		"static u32 netmsgReadPlayerStats(struct netbuf *src, struct netclient *srccl,");

	const size_t ammomask = body.find("const u32 ammomask = netbufReadU32(src);");
	const size_t attacker = body.find("const s8 wire_attacker_id = netbufReadS8(src);");
	const size_t first_check = body.find("if (src->error || clid >= NET_MAX_CLIENTS + 1)");
	const size_t no_player = body.find("if (!pl || !pl->prop || !pl->prop->chr)");
	REQUIRE(ammomask != std::string::npos);
	REQUIRE(attacker != std::string::npos);
	REQUIRE(first_check != std::string::npos);
	REQUIRE(no_player != std::string::npos);
	REQUIRE(ammomask < first_check);
	REQUIRE(attacker < first_check);
	// No buffer read once the player has been checked
	REQUIRE(body.find("netbufRead", no_player) == std::string::npos);
}

TEST_CASE("net readers: SVC_PROP_MOVE reads its projectile tail before it decides",
          "[net][b1131]") {
	const std::string body = function_body(netmsg_code(),
		"u32 netmsgSvcPropMoveRead(struct netbuf *src, struct netclient *srccl)\n{");

	const size_t tail = body.find("netbufReadCoord(src, &pspeed);");
	const size_t skip = body.find("if (!prop || srccl->state < CLSTATE_GAME)");
	REQUIRE(tail != std::string::npos);
	REQUIRE(skip != std::string::npos);
	REQUIRE(tail < skip);
	REQUIRE(body.find("netbufRead", skip) == std::string::npos);
}

TEST_CASE("Grid Fiesta consumes full spawn stats before applying a new sealed life",
          "[net][grid][reconnect][b1131]") {
	const std::string packet = function_body(netmsg_code(), "u32 netmsgSvcGridFiestaSpawnRead(");
	const std::string stats = function_body(netmsg_code(), "static u32 netmsgReadPlayerStats(");
	REQUIRE(packet.find("netbufReadU32(src)") < packet.find("netmsgReadPlayerStats"));
	REQUIRE(packet.find("netbufReadU16(src)") < packet.find("netmsgReadPlayerStats"));
	REQUIRE(packet.find("netbufReadCoord(src, &fiesta.pose.position)") < packet.find("netmsgReadPlayerStats"));
	REQUIRE(packet.find("fiesta.pose.room = netbufReadS16(src)") < packet.find("netmsgReadPlayerStats"));
	const size_t tail = stats.find("const s8 wire_attacker_id = netbufReadS8(src)");
	const size_t duplicate = stats.find("fiesta->life < life->number");
	const size_t sealed = stats.find("gridMatchRuntimeFiestaWeaponId(weapon, &slot)");
	const size_t reset = stats.find("playerStartNewGridFiestaLife(&fiesta->pose)");
	const size_t grant = stats.find("playerGiveSpawnWeapon(slot, weapon)");
	const size_t commit = stats.find(".number = fiesta->life", grant);
	const size_t ammo = stats.find("pl->ammoheldarr[i] = ammoheld[i]");
	REQUIRE(tail < duplicate);
	REQUIRE(duplicate < sealed);
	REQUIRE(sealed < reset);
	REQUIRE(reset < grant);
	REQUIRE(grant < commit);
	REQUIRE(commit < ammo);
	REQUIRE(ammo != std::string::npos);
	REQUIRE(stats.find("snapshot && !s_ReconnectPropReceive.complete") != std::string::npos);
	REQUIRE(stats.find("fiesta->life == life->number && (!release_dead || life->release_dead)", duplicate) < sealed);
	REQUIRE(stats.find("if (!snapshot && !release_dead)", duplicate) < sealed);
	REQUIRE(stats.find("playerSpawnPoseIsValid(&fiesta->pose)", duplicate) < sealed);
	REQUIRE(stats.find("s_ReconnectPropReceive.complete || release_dead", ammo) != std::string::npos);
	REQUIRE(stats.find(".release_dead = true", ammo) != std::string::npos);
}

TEST_CASE("net readers: SVC_CHR_DISARM reads its position before it decides, and the writer always sends it",
          "[net][b1131]") {
	const std::string read = function_body(netmsg_code(),
		"u32 netmsgSvcChrDisarmRead(struct netbuf *src, struct netclient *srccl)\n{");
	const std::string write = function_body(netmsg_code(),
		"u32 netmsgSvcChrDisarmWrite(struct netbuf *dst, struct chrdata *chr, struct prop *aprop, u8 weaponnum, f32 wpndamage, struct coord *wpnpos)\n{");

	const size_t pos = read.find("netbufReadCoord(src, &pos);");
	const size_t first_check = read.find("if (src->error)");
	REQUIRE(pos != std::string::npos);
	REQUIRE(first_check != std::string::npos);
	REQUIRE(pos < first_check);
	REQUIRE(read.find("netbufRead", first_check) == std::string::npos);

	REQUIRE(write.find("if (wpndamage > 0.f) {") != std::string::npos);
	REQUIRE(write.find("wpndamage > 0.f && wpnpos") == std::string::npos);
}

TEST_CASE("net readers: SVC_PROP_SPAWN parses its whole row before it decides",
          "[net][b1131]") {
	const std::string body = function_body(netmsg_code(),
		"u32 netmsgSvcPropSpawnRead(struct netbuf *src, struct netclient *srccl)\n{");

	// Every read (header, type fields, object block, projectile block) comes
	// before the first decision about this client's state
	const size_t decide = body.find("if (srccl->state < CLSTATE_GAME)");
	REQUIRE(decide != std::string::npos);
	REQUIRE(body.find("netbufRead", decide) == std::string::npos);
	REQUIRE(body.find("netRead", decide) == std::string::npos);
	REQUIRE(body.find("netbufReadMtxf(src, &pmtx);") < decide);

	// Outside a reconnect a declined row is skipped; inside one it fails closed
	const size_t warn = body.find("\"NET: no prop allocated when spawning prop %u (%u)\"");
	REQUIRE(warn != std::string::npos);
	const std::string expected = "return reconnect ? 1 : 0;";
	const size_t ret = body.find("return", warn);
	REQUIRE(ret != std::string::npos);
	REQUIRE(body.compare(ret, expected.size(), expected) == 0);
	REQUIRE(body.find("NET: SVC_PROP_SPAWN skipped syncid=%u type=%u objtype=%u %s") != std::string::npos);
}

TEST_CASE("net readers: SVC_PROP_SPAWN writes the projectile bit only with a projectile",
          "[net][b1131]") {
	const std::string write = function_body(netmsg_code(),
		"u32 netmsgSvcPropSpawnWrite(struct netbuf *dst, struct prop *prop)\n{");

	const size_t clear = write.find("hidden &= ~OBJHFLAG_PROJECTILE;");
	const size_t written = write.find("netbufWriteHidden(dst, hidden);");
	const size_t block = write.find("if (hidden & OBJHFLAG_PROJECTILE) {");
	REQUIRE(clear != std::string::npos);
	REQUIRE(written != std::string::npos);
	REQUIRE(block != std::string::npos);
	REQUIRE(clear < written);
	REQUIRE(written < block);
	REQUIRE(write.find("netbufWriteHidden(dst, prop->obj->hidden);") == std::string::npos);
}

TEST_CASE("net readers: pickup, use and damage skip a message they cannot apply",
          "[net][b1131]") {
	const char *signatures[] = {
		"u32 netmsgSvcPropPickupRead(struct netbuf *src, struct netclient *srccl)\n{",
		"u32 netmsgSvcPropUseRead(struct netbuf *src, struct netclient *srccl)\n{",
		"u32 netmsgSvcChrDamageRead(struct netbuf *src, struct netclient *srccl, s32 grid)\n{",
	};
	const char *skips[] = {
		"NET: SVC_PROP_PICKUP skipped",
		"NET: SVC_PROP_USE skipped",
		"NET: SVC_CHR_DAMAGE skipped",
	};

	for (size_t i = 0; i < 3; i++) {
		const std::string body = function_body(netmsg_code(), signatures[i]);
		const size_t skip = body.find(skips[i]);

		INFO("reader " << signatures[i]);
		REQUIRE(skip != std::string::npos);
		const size_t ret = body.find("return", skip);
		REQUIRE(ret != std::string::npos);
		REQUIRE(body.compare(ret, 9, "return 0;") == 0);
		// The message was fully read before the skip
		REQUIRE(body.find("netbufRead", skip) == std::string::npos);
	}
}

/* Third pass: the resync transaction (net.c netServerPublishPendingResyncs)
 * sends character, prop, score and NPC resyncs, the stage flags, objective
 * statuses and the scenario baseline in one packet, so a short row in any of
 * them garbles everything after it. */

TEST_CASE("net writers: SVC_PLAYER_SCORES announces only the rows it writes",
          "[net][b1131]") {
	const std::string write = function_body(netmsg_code(),
		"u32 netmsgSvcPlayerScoresWrite(struct netbuf *dst)\n{");

	const size_t counted = write.find("if (g_MpAllChrConfigPtrs[i]) {");
	const size_t header = write.find("netbufWriteU8(dst, SVC_PLAYER_SCORES);");
	const size_t written = write.find("netbufWriteU8(dst, count);");
	REQUIRE(counted != std::string::npos);
	REQUIRE(header != std::string::npos);
	REQUIRE(written != std::string::npos);
	REQUIRE(counted < header);
	REQUIRE(header < written);
	REQUIRE(write.find("netbufWriteU8(dst, g_MpNumChrs);") == std::string::npos);
}

TEST_CASE("net writers: SVC_CHR_RESYNC writes whole rows and counts only them",
          "[net][b1131]") {
	const std::string write = function_body(netmsg_code(),
		"u32 netmsgSvcChrResyncWrite(struct netbuf *dst)\n{");

	const size_t counted = write.find("if (chr && chr->prop && chr->aibot) {");
	const size_t written = write.find("netbufWriteU8(dst, count);");
	REQUIRE(counted != std::string::npos);
	REQUIRE(written != std::string::npos);
	REQUIRE(counted < written);
	REQUIRE(write.find("netbufWriteU8(dst, (u8)g_BotCount);") == std::string::npos);
	// No short placeholder row: the loop skips straight to the next simulant
	const size_t skip = write.find("if (!chr || !chr->prop || !chr->aibot) {", written);
	REQUIRE(skip != std::string::npos);
	const size_t next_stmt = write.find_first_not_of(" \t\r\n", skip + std::string("if (!chr || !chr->prop || !chr->aibot) {").size());
	REQUIRE(next_stmt != std::string::npos);
	REQUIRE(write.compare(next_stmt, 9, "continue;") == 0);
}

TEST_CASE("net readers: the character and prop resyncs read every row before skipping",
          "[net][b1131]") {
	const char *signatures[] = {
		"u32 netmsgSvcChrResyncRead(struct netbuf *src, struct netclient *srccl)\n{",
		"u32 netmsgSvcPropResyncRead(struct netbuf *src, struct netclient *srccl)\n{",
	};

	for (const char *signature : signatures) {
		const std::string body = function_body(netmsg_code(), signature);

		INFO("reader " << signature);
		// The client-state check no longer returns from inside the row loop
		REQUIRE(body.find("if (src->error || srccl->state < CLSTATE_GAME) {") == std::string::npos);
		const size_t skip = body.find("if (srccl->state < CLSTATE_GAME");
		REQUIRE(skip != std::string::npos);
		const size_t open = body.find('{', skip);
		REQUIRE(open != std::string::npos);
		const size_t next_stmt = body.find_first_not_of(" \t\r\n", open + 1);
		REQUIRE(next_stmt != std::string::npos);
		REQUIRE(body.compare(next_stmt, 9, "continue;") == 0);
	}
}

TEST_CASE("net readers: a declined NPC resync costs only that message",
          "[net][b1131]") {
	const std::string body = function_body(netmsg_code(),
		"u32 netmsgSvcNpcResyncRead(struct netbuf *src, struct netclient *srccl)\n{");

	const size_t incomplete = body.find("if (!complete) {");
	const size_t rewind = body.find("src->rp = records_rp;");
	REQUIRE(incomplete != std::string::npos);
	REQUIRE(rewind != std::string::npos);
	REQUIRE(incomplete < rewind);
	const std::string branch = body.substr(incomplete, rewind - incomplete);
	REQUIRE(branch.find("g_NetPendingResyncReqFlags |= NET_RESYNC_FLAG_NPCS;") != std::string::npos);
	REQUIRE(branch.find("return 0;") != std::string::npos);
	REQUIRE(branch.find("return 1;") == std::string::npos);
}

TEST_CASE("net readers: CLC_ADMIN consumes a rejected command's arguments",
          "[net][b1131]") {
	const std::string skip = function_body(netmsg_code(),
		"static u32 netmsgClcAdminSkipArgs(struct netbuf *src, u8 subcode)\n{");
	const std::string read = function_body(netmsg_code(),
		"u32 netmsgClcAdminRead(struct netbuf *src, struct netclient *srccl)\n{");

	// The layout in server_admin.h: KICK and BAN carry a u8 and a string,
	// UNBAN a string, AUTH, LIST and STATUS nothing; anything else is unknown
	const size_t kick = skip.find("case ADMIN_SUB_KICK:");
	const size_t ban = skip.find("case ADMIN_SUB_BAN:", kick);
	const size_t kick_u8 = skip.find("(void)netbufReadU8(src);", ban);
	const size_t kick_str = skip.find("(void)netbufReadStr(src);", kick_u8);
	const size_t unban = skip.find("case ADMIN_SUB_UNBAN:", kick_str);
	const size_t unban_str = skip.find("(void)netbufReadStr(src);", unban);
	const size_t unknown = skip.find("default:", unban_str);
	REQUIRE(kick != std::string::npos);
	REQUIRE(ban != std::string::npos);
	REQUIRE(kick_u8 != std::string::npos);
	REQUIRE(kick_str != std::string::npos);
	REQUIRE(unban != std::string::npos);
	REQUIRE(unban_str != std::string::npos);
	REQUIRE(unknown != std::string::npos);
	REQUIRE(skip.find("return 1;", unknown) != std::string::npos);
	for (const char *none : {"case ADMIN_SUB_AUTH:", "case ADMIN_SUB_LIST:", "case ADMIN_SUB_STATUS:"}) {
		INFO("subcode " << none);
		REQUIRE(skip.find(none) != std::string::npos);
		REQUIRE(skip.find(none) < kick);
	}

	// A non-server peer and an unauthenticated one skip the arguments
	REQUIRE(read.find("return netmsgClcAdminSkipArgs(src, subcode);") != std::string::npos);
	const size_t not_admin = read.find("if (!srccl->is_admin) {");
	REQUIRE(not_admin != std::string::npos);
	const size_t consumed = read.find("netmsgClcAdminSkipArgs(src, subcode);", not_admin);
	const size_t reply = read.find("ADMIN_RESP_NOT_AUTH", not_admin);
	REQUIRE(consumed != std::string::npos);
	REQUIRE(reply != std::string::npos);
	REQUIRE(consumed < reply);
	REQUIRE(read.find("return skipped;", reply) != std::string::npos);

	// An unknown subcode stops the packet: its length is not known
	const size_t unknown_reply = read.find("\"Unknown admin subcode.\"");
	REQUIRE(unknown_reply != std::string::npos);
	const size_t unknown_ret = read.find("return", unknown_reply);
	REQUIRE(unknown_ret != std::string::npos);
	REQUIRE(read.compare(unknown_ret, 9, "return 1;") == 0);
}
