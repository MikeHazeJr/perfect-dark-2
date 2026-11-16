/*
 * tests/test_bool_layout_static.cpp
 *
 * Static guards for the include-order `bool` hazard (SP-75).
 *
 * Project `bool` is a macro: `#define bool s32` in src/include/types.h. A C
 * file that includes <stdbool.h> after types.h gets `bool` redefined as the
 * 1-byte `_Bool` for the rest of that file, so a project struct with `bool`
 * members is laid out differently there than everywhere else, and a `bool`
 * prototype or global changes width. On 2026-11-14 port/src/main.c and
 * port/src/smoke_harness.c reached <stdbool.h> through native_write_policy.h
 * just before net/net.h and saw struct netclient with twelve 1-byte flags
 * while net.c and netmsg.c saw 4-byte ones, so &g_NetClients[1] differed
 * between files (B-1120). The same audit found 28 C files flipping `bool` and
 * 291 declarations with two widths.
 *
 * The guards:
 *   1. The shared network structs in net.h declare no `bool` member.
 *   2. No project header or C source includes <stdbool.h>. Vendored code
 *      (port/external, port/include/external, imgui, glad) is exempt; ENet's
 *      flip is undone by net/netenet.h. A C file that needs `bool` without the
 *      game's precompiled header includes "pd_bool.h" instead.
 *   3. The fast3d headers that the C++ renderer shares with C files keep a
 *      scoped one-byte `bool`: push_macro, `_Bool` in C, the keyword in C++,
 *      pop_macro, with every `bool` of the header inside the scope.
 *   4. Headers included by both C and C++ files that used to pull in
 *      <stdbool.h> spell their boolean results s32, so both languages agree.
 *
 * tools/bool_layout_audit.py checks the preprocessed result of every file in
 * CI; these tests are the fast source-shape tripwire. They do not prove
 * runtime behavior.
 */

#include "catch.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <string>

namespace {

std::string read_text_bool_layout(const std::string &path)
{
	std::ifstream file(path, std::ios::binary);
	INFO("path: " << path);
	REQUIRE(file.good());
	std::ostringstream buffer;
	buffer << file.rdbuf();
	return buffer.str();
}

std::string struct_body_bool_layout(const std::string &text, const std::string &head)
{
	const size_t start = text.find(head);
	INFO("missing struct: " << head);
	REQUIRE(start != std::string::npos);
	const size_t open = text.find('{', start);
	REQUIRE(open != std::string::npos);
	size_t depth = 0;
	for (size_t i = open; i < text.size(); ++i) {
		if (text[i] == '{') {
			++depth;
		} else if (text[i] == '}') {
			--depth;
			if (depth == 0) {
				return text.substr(open, i - open + 1);
			}
		}
	}
	FAIL("unterminated struct " << head);
	return std::string();
}

/* Blanks comments (keeping newlines) so a comment mentioning `bool` or
 * <stdbool.h> is not code. String literals are kept: the push_macro pragmas
 * name "bool" in one. */
std::string strip_comments_bool_layout(const std::string &text)
{
	std::string out;
	out.reserve(text.size());
	size_t i = 0;
	while (i < text.size()) {
		const char c = text[i];
		if (c == '/' && i + 1 < text.size() && text[i + 1] == '/') {
			while (i < text.size() && text[i] != '\n') {
				++i;
			}
		} else if (c == '/' && i + 1 < text.size() && text[i + 1] == '*') {
			i += 2;
			while (i < text.size() && !(text[i] == '*' && i + 1 < text.size() && text[i + 1] == '/')) {
				if (text[i] == '\n') {
					out += '\n';
				}
				++i;
			}
			i = std::min(i + 2, text.size());
			out += ' ';
		} else if (c == '"') {
			out += text[i++];
			while (i < text.size() && text[i] != '"' && text[i] != '\n') {
				if (text[i] == '\\' && i + 1 < text.size()) {
					out += text[i++];
				}
				out += text[i++];
			}
			if (i < text.size() && text[i] == '"') {
				out += text[i++];
			}
		} else {
			out += c;
			++i;
		}
	}
	return out;
}

/* True when one line of comment-free code is `#include <stdbool.h>` (or with
 * quotes). A plain scan: std::regex over whole source files is slow and can
 * recurse deeply in libstdc++. */
bool includes_stdbool_bool_layout(const std::string &code)
{
	std::istringstream lines(code);
	std::string line;
	while (std::getline(lines, line)) {
		size_t i = line.find_first_not_of(" \t");
		if (i == std::string::npos || line[i] != '#') {
			continue;
		}
		i = line.find_first_not_of(" \t", i + 1);
		if (i == std::string::npos || line.compare(i, 7, "include") != 0) {
			continue;
		}
		i = line.find_first_not_of(" \t", i + 7);
		if (i != std::string::npos && (line.compare(i, 12, "<stdbool.h>") == 0
				|| line.compare(i, 12, "\"stdbool.h\"") == 0)) {
			return true;
		}
	}
	return false;
}

bool is_vendored_bool_layout(const std::string &path)
{
	for (const char *prefix : {"port/external/", "port/include/external/",
			"port/fast3d/imgui/", "port/fast3d/glad/"}) {
		if (path.rfind(prefix, 0) == 0) {
			return true;
		}
	}
	return false;
}

const char *const k_scoped_gfx_headers[] = {
	"port/fast3d/gfx_api.h",
	"port/fast3d/gfx_cc.h",
	"port/fast3d/gfx_pc.h",
	"port/fast3d/gfx_rendering_api.h",
	"port/fast3d/gfx_window_manager_api.h",
};

} // namespace

TEST_CASE("Shared network structs spell their flags s32, not bool",
          "[net][static][bool-layout]")
{
	const std::string net = read_text_bool_layout("port/include/net/net.h");
	static const std::regex bool_member(R"((^|[;{\s])bool\s+\w+\s*;)");

	for (const char *head : {"struct netpreservedplayer {", "struct netrecentserver {",
			"struct netclient {"}) {
		const std::string body = strip_comments_bool_layout(struct_body_bool_layout(net, head));
		INFO("struct: " << head);
		REQUIRE_FALSE(std::regex_search(body, bool_member));
	}

	/* The flags the October reconnect and movement work added are present
	 * with the fixed type. */
	const std::string client = strip_comments_bool_layout(
		struct_body_bool_layout(net, "struct netclient {"));
	for (const char *field : {"s32 inmove_valid;", "s32 inmove_pending;", "s32 inmoveprev_valid;",
			"s32 inmovetick_valid;", "s32 outmoveack_valid;", "s32 reconnect_settings_pending;",
			"s32 server_disconnect_intent_pending;", "s32 reconnect_resync_pending;",
			"s32 reconnect_gameplay_witness_pending;", "s32 stage_ready;", "s32 is_admin;"}) {
		INFO("field: " << field);
		REQUIRE(client.find(field) != std::string::npos);
	}
}

TEST_CASE("No project header or C source includes stdbool.h",
          "[static][bool-layout]")
{
	namespace fs = std::filesystem;

	/* The scanner itself. */
	REQUIRE(includes_stdbool_bool_layout("#include <stdbool.h>\n"));
	REQUIRE(includes_stdbool_bool_layout("int x;\n  #  include \"stdbool.h\"\n"));
	REQUIRE_FALSE(includes_stdbool_bool_layout("#include <stdint.h>\n"));
	REQUIRE_FALSE(includes_stdbool_bool_layout("x = 1; /* #include <stdbool.h> */\n"));

	size_t scanned = 0;
	std::set<std::string> found;
	for (const char *root : {"src", "include", "port"}) {
		if (!fs::exists(root)) {
			continue;
		}
		for (const auto &entry : fs::recursive_directory_iterator(root)) {
			if (!entry.is_regular_file()) {
				continue;
			}
			const std::string ext = entry.path().extension().string();
			/* C++ files are exempt: there <stdbool.h> leaves `bool` alone. */
			if (ext != ".h" && ext != ".hpp" && ext != ".c" && ext != ".inc") {
				continue;
			}
			const std::string path = entry.path().generic_string();
			if (is_vendored_bool_layout(path)) {
				continue;
			}
			++scanned;
			if (includes_stdbool_bool_layout(strip_comments_bool_layout(read_text_bool_layout(path)))) {
				found.insert(path);
			}
		}
	}

	INFO("files scanned: " << scanned);
	REQUIRE(scanned > 500);
	for (const std::string &path : found) {
		INFO("includes <stdbool.h>; use \"pd_bool.h\" in C files, s32 in shared "
		     "headers, or the scoped pattern of the fast3d headers: " << path);
		CHECK(false);
	}
}

TEST_CASE("fast3d headers shared with C keep a scoped one-byte bool",
          "[static][bool-layout]")
{
	static const std::regex first_line(R"(^\s*#\s*undef\s+bool\s*\n)");
	static const std::regex c_meaning(
		R"(#\s*ifndef\s+__cplusplus\s*\n\s*#\s*define\s+bool\s+_Bool\s*\n\s*#\s*endif)");
	static const std::regex bool_token(R"((^|[^\w])bool([^\w]|$))");
	const std::string push = "#pragma push_macro(\"bool\")";
	const std::string pop = "#pragma pop_macro(\"bool\")";

	for (const char *path : k_scoped_gfx_headers) {
		INFO("header: " << path);
		const std::string code = strip_comments_bool_layout(read_text_bool_layout(path));
		const size_t push_at = code.find(push);
		const size_t pop_at = code.find(pop);
		REQUIRE(push_at != std::string::npos);
		REQUIRE(pop_at != std::string::npos);
		REQUIRE(code.find(push, push_at + 1) == std::string::npos);
		REQUIRE(code.find(pop, pop_at + 1) == std::string::npos);
		REQUIRE(push_at < pop_at);

		/* The scope starts by dropping the includer's macro. */
		const size_t body_at = code.find('\n', push_at);
		REQUIRE(body_at != std::string::npos);
		REQUIRE(body_at < pop_at);
		const std::string scope = code.substr(body_at + 1, pop_at - body_at - 1);
		REQUIRE(std::regex_search(scope, first_line));

		/* gfx_pc.h is C++ only; the others give C the one-byte _Bool. */
		if (std::string(path) != "port/fast3d/gfx_pc.h") {
			REQUIRE(std::regex_search(scope, c_meaning));
		}

		/* Every bool of the header is inside the scope. */
		const std::string before = code.substr(0, push_at);
		const std::string after = code.substr(pop_at + pop.size());
		CHECK_FALSE(std::regex_search(before, bool_token));
		CHECK_FALSE(std::regex_search(after, bool_token));
	}
}

TEST_CASE("Headers shared by C and C++ spell boolean results s32",
          "[static][bool-layout]")
{
	static const std::regex bool_token(R"((^|[^\w])bool([^\w]|$))");
	for (const char *path : {"port/include/native_write_policy.h",
			"port/include/native_write_policy_internal.h",
			"port/include/net/net_manifest_type.h",
			"port/include/bondgun_cache.h",
			"port/include/catalog_checked.h"}) {
		INFO("header: " << path);
		const std::string code = strip_comments_bool_layout(read_text_bool_layout(path));
		CHECK_FALSE(std::regex_search(code, bool_token));
	}

	/* pd_bool.h gives C files the project bool without touching C++. */
	const std::string pd_bool = strip_comments_bool_layout(
		read_text_bool_layout("port/include/pd_bool.h"));
	static const std::regex c_only(
		R"(#\s*ifndef\s+__cplusplus\s*\n\s*#\s*ifndef\s+bool\s*\n\s*#\s*define\s+bool\s+s32)");
	CHECK(std::regex_search(pd_bool, c_only));
	static const std::regex include_stdbool(R"(#[ \t]*include[ \t]*[<"]stdbool\.h[>"])");
	CHECK_FALSE(std::regex_search(pd_bool, include_stdbool));
}
