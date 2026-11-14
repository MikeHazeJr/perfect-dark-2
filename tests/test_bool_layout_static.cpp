/*
 * tests/test_bool_layout_static.cpp
 *
 * Static guards for the include-order `bool` hazard.
 *
 * Project `bool` is a macro: `#define bool s32` in src/include/types.h. A C
 * file that includes <stdbool.h> after types.h gets `bool` redefined as the
 * 1-byte `_Bool` for the rest of that file, so a project struct with `bool`
 * members is laid out differently there than everywhere else. On 2026-11-14
 * port/src/main.c and port/src/smoke_harness.c reached <stdbool.h> through
 * native_write_policy.h just before net/net.h and saw struct netclient with
 * twelve 1-byte flags while net.c and netmsg.c saw 4-byte ones, so
 * &g_NetClients[1] differed between files. That broke the listen-host match
 * smoke (the host autostart hook in main.c never saw the joined client).
 *
 * The guards:
 *   1. The shared network structs in net.h declare no `bool` member; their
 *      flags are spelled s32 so one layout holds in every file.
 *   2. Tripwire: the set of project headers that include <stdbool.h> must not
 *      grow. Each listed header is a known flip source (or a C/C++ fast3d
 *      header whose structs are 1-byte `bool` in both languages). Removing an
 *      entry is welcome; adding one needs the layout review this file
 *      describes, then an update to the list with the reason.
 *
 * Source-shape checks only; they do not prove runtime behavior.
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

/* Removes comments so a comment mentioning `bool` is not a member. */
std::string strip_comments_bool_layout(const std::string &text)
{
	static const std::regex comments(R"(//[^\n]*|/\*[\s\S]*?\*/)");
	return std::regex_replace(text, comments, " ");
}

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

TEST_CASE("The set of project headers that include stdbool.h does not grow",
          "[static][bool-layout]")
{
	namespace fs = std::filesystem;
	static const std::set<std::string> known = {
		/* C/C++ shared fast3d headers: 1-byte bool in both languages, and
		 * every C includer audited on 2026-11-14 sees them as _Bool. */
		"port/fast3d/gfx_api.h",
		"port/fast3d/gfx_cc.h",
		"port/fast3d/gfx_pc.h",
		"port/fast3d/gfx_rendering_api.h",
		"port/fast3d/gfx_window_manager_api.h",
		/* Vendored ENet amalgamation. */
		"port/include/external/enet.h",
		/* Known flip sources in C files (2026-11-14 audit: 27 C files flip
		 * `bool` to _Bool through these). Fixing them changes `bool` in the
		 * code that follows them in those files; see the week multiplayer
		 * plan for the decision. Do not add to this group. */
		"port/include/bondgun_cache.h",
		"port/include/catalog_checked.h",
		"port/include/native_write_policy.h",
		"port/include/net/net_manifest_type.h",
	};
	static const std::regex include_stdbool(R"((^|\n)[ \t]*#[ \t]*include[ \t]*<stdbool\.h>)");

	std::set<std::string> found;
	for (const char *root : {"src/include", "include", "port/include", "port/fast3d", "port/src"}) {
		if (!fs::exists(root)) {
			continue;
		}
		for (const auto &entry : fs::recursive_directory_iterator(root)) {
			if (!entry.is_regular_file()) {
				continue;
			}
			const std::string ext = entry.path().extension().string();
			if (ext != ".h" && ext != ".hpp") {
				continue;
			}
			std::string path = entry.path().generic_string();
			if (path.find("/imgui/") != std::string::npos) {
				continue;
			}
			if (std::regex_search(read_text_bool_layout(path), include_stdbool)) {
				found.insert(path);
			}
		}
	}

	for (const std::string &path : found) {
		INFO("header includes <stdbool.h> but is not in the reviewed list: " << path);
		CHECK(known.count(path) == 1);
	}
}
