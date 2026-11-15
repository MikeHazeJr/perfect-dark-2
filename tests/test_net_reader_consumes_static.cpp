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
		"u32 netmsgSvcPlayerStatsRead(struct netbuf *src, struct netclient *srccl)\n{");

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

TEST_CASE("net readers: SVC_PROP_SPAWN does not return 0 with its object fields unread",
          "[net][b1131]") {
	const std::string body = function_body(netmsg_code(),
		"u32 netmsgSvcPropSpawnRead(struct netbuf *src, struct netclient *srccl)\n{");

	const size_t warn = body.find("\"NET: no prop allocated when spawning prop %u (%u)\"");
	REQUIRE(warn != std::string::npos);
	const std::string expected = "return src->error ? src->error : 1;";
	const size_t ret = body.find("return", warn);
	REQUIRE(ret != std::string::npos);
	REQUIRE(body.compare(ret, expected.size(), expected) == 0);
}

TEST_CASE("net readers: pickup, use and damage skip a message they cannot apply",
          "[net][b1131]") {
	const char *signatures[] = {
		"u32 netmsgSvcPropPickupRead(struct netbuf *src, struct netclient *srccl)\n{",
		"u32 netmsgSvcPropUseRead(struct netbuf *src, struct netclient *srccl)\n{",
		"u32 netmsgSvcChrDamageRead(struct netbuf *src, struct netclient *srccl)\n{",
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
