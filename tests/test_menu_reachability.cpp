/*
 * test_menu_reachability.cpp -- Reachability invariants (cohort 2).
 *
 * Per Mike's directive: "ability to get to every menu option that does
 * something and not the ones that don't."
 *
 * The flat-menu-navigation methodology (context/designs/flat-menu-navigation.md)
 * codifies that focus traverses across panel containers transparently, and
 * that every interactive control is reachable while non-interactive
 * widgets (labels, separators, disabled controls) are NEVER focused.
 *
 * This suite walks a synthetic menu tree using a pure-C "next interactive
 * in document order" iterator and asserts:
 *   - Every interactive leaf is visited exactly once across one full
 *     forward traversal.
 *   - No non-interactive leaf is ever returned by the iterator.
 *   - Disabled controls are skipped (their on-click is a no-op even if
 *     ImGui visually registers focus).
 *   - Panel containers are transparent: stepping forward from a leaf in
 *     panel A lands on the next leaf in panel B without an "engage panel"
 *     intermediate state.
 *
 * The iterator IS the spec. If the project's ImGui menus diverge from
 * "next interactive in DOM order, panels transparent", a follow-up will
 * either fix the menu or update the spec - the test then enforces it.
 *
 * @SYNC context/designs/flat-menu-navigation.md Rule 1 (NavFlattened)
 * @SYNC context/designs/flat-menu-navigation.md Rule 3 (A acts on focused)
 */

#include "catch.hpp"
#include <vector>
#include <string>

namespace {

enum NodeKind {
    NODE_PANEL,        /* container; transparent to focus traversal */
    NODE_INTERACTIVE,  /* button / checkbox / slider - A acts here */
    NODE_DISABLED,     /* visually rendered but not focusable */
    NODE_SEPARATOR,    /* purely visual, never focusable */
    NODE_LABEL,        /* purely visual, never focusable */
};

struct MenuNode {
    NodeKind kind;
    std::string id;
    std::vector<MenuNode> children;  /* only meaningful for PANEL */

    static MenuNode panel(const std::string &id, std::vector<MenuNode> &&children) {
        MenuNode n; n.kind = NODE_PANEL; n.id = id;
        n.children = std::move(children); return n;
    }
    static MenuNode interactive(const std::string &id) {
        MenuNode n; n.kind = NODE_INTERACTIVE; n.id = id; return n;
    }
    static MenuNode disabled(const std::string &id) {
        MenuNode n; n.kind = NODE_DISABLED; n.id = id; return n;
    }
    static MenuNode separator() {
        MenuNode n; n.kind = NODE_SEPARATOR; n.id = "---"; return n;
    }
    static MenuNode label(const std::string &id) {
        MenuNode n; n.kind = NODE_LABEL; n.id = id; return n;
    }
};

/* Recursive DOM-order walk that emits only interactive leaves.
 * This is the CONTRACT every flat menu must satisfy: D-pad advances to
 * the next interactive control transparently across panel boundaries. */
void collectInteractive(const MenuNode &node, std::vector<std::string> &out)
{
    switch (node.kind) {
    case NODE_PANEL:
        for (const auto &child : node.children) {
            collectInteractive(child, out);
        }
        break;
    case NODE_INTERACTIVE:
        out.push_back(node.id);
        break;
    case NODE_DISABLED:
    case NODE_SEPARATOR:
    case NODE_LABEL:
        /* Skipped - never reachable via D-pad. */
        break;
    }
}

} /* namespace */

TEST_CASE("reachability: every interactive node is visited exactly once",
          "[reachability][flat-menu]")
{
    auto tree = MenuNode::panel("root", {
        MenuNode::interactive("scenario_picker"),
        MenuNode::interactive("arena_picker"),
        MenuNode::panel("limits", {
            MenuNode::label("Limits"),
            MenuNode::interactive("time_limit"),
            MenuNode::interactive("score_limit"),
        }),
        MenuNode::interactive("start_match"),
    });

    std::vector<std::string> visited;
    collectInteractive(tree, visited);
    REQUIRE(visited.size() == 5);
    REQUIRE(visited[0] == "scenario_picker");
    REQUIRE(visited[1] == "arena_picker");
    REQUIRE(visited[2] == "time_limit");
    REQUIRE(visited[3] == "score_limit");
    REQUIRE(visited[4] == "start_match");
}

TEST_CASE("reachability: separators and labels are never returned",
          "[reachability][flat-menu]")
{
    auto tree = MenuNode::panel("root", {
        MenuNode::label("Section: Audio"),
        MenuNode::separator(),
        MenuNode::interactive("master_volume"),
        MenuNode::separator(),
        MenuNode::label("Section: Music"),
        MenuNode::interactive("music_volume"),
        MenuNode::separator(),
    });

    std::vector<std::string> visited;
    collectInteractive(tree, visited);
    REQUIRE(visited.size() == 2);
    REQUIRE(visited[0] == "master_volume");
    REQUIRE(visited[1] == "music_volume");

    /* No separator or label appears in the visited set. */
    for (const auto &id : visited) {
        REQUIRE(id != "---");
        REQUIRE(id.find("Section:") == std::string::npos);
    }
}

TEST_CASE("reachability: disabled controls are skipped",
          "[reachability][flat-menu][disabled]")
{
    /* If a "Start Match" button is disabled because the room is not
     * ready (no scenario picked, no human players, etc.), focus must
     * NOT land on it. Disabled controls are visible but not interactive. */
    auto tree = MenuNode::panel("room", {
        MenuNode::interactive("scenario_picker"),
        MenuNode::disabled("start_match"),  /* not ready yet */
        MenuNode::interactive("leave_room"),
    });

    std::vector<std::string> visited;
    collectInteractive(tree, visited);
    REQUIRE(visited.size() == 2);
    REQUIRE(visited[0] == "scenario_picker");
    REQUIRE(visited[1] == "leave_room");
}

TEST_CASE("reachability: nested panels are transparent",
          "[reachability][flat-menu][panel-transparency]")
{
    /* The flat-menu rule (R1): D-pad-right from a control in panel A
     * goes directly to a control in panel B without an "engage panel"
     * step. Modelled here as the iterator producing a flat sequence
     * regardless of panel nesting depth. */
    auto tree = MenuNode::panel("page", {
        MenuNode::panel("left_column", {
            MenuNode::panel("members_panel", {
                MenuNode::interactive("member_row_1"),
                MenuNode::interactive("member_row_2"),
                MenuNode::interactive("add_bot_btn"),
            }),
        }),
        MenuNode::panel("right_column", {
            MenuNode::panel("settings_panel", {
                MenuNode::interactive("scenario"),
                MenuNode::interactive("arena"),
                MenuNode::panel("limits_section", {
                    MenuNode::interactive("time_limit"),
                    MenuNode::interactive("score_limit"),
                }),
            }),
            MenuNode::interactive("start_match"),
        }),
    });

    std::vector<std::string> visited;
    collectInteractive(tree, visited);
    REQUIRE(visited.size() == 8);
    REQUIRE(visited[0] == "member_row_1");
    REQUIRE(visited[1] == "member_row_2");
    REQUIRE(visited[2] == "add_bot_btn");
    REQUIRE(visited[3] == "scenario");
    REQUIRE(visited[4] == "arena");
    REQUIRE(visited[5] == "time_limit");
    REQUIRE(visited[6] == "score_limit");
    REQUIRE(visited[7] == "start_match");
}

TEST_CASE("reachability: empty panels do not break traversal",
          "[reachability][flat-menu]")
{
    auto tree = MenuNode::panel("root", {
        MenuNode::panel("empty_left", {}),
        MenuNode::interactive("only_button"),
        MenuNode::panel("empty_right", {}),
    });

    std::vector<std::string> visited;
    collectInteractive(tree, visited);
    REQUIRE(visited.size() == 1);
    REQUIRE(visited[0] == "only_button");
}

TEST_CASE("reachability: panel containing only labels is skipped entirely",
          "[reachability][flat-menu]")
{
    auto tree = MenuNode::panel("root", {
        MenuNode::interactive("first"),
        MenuNode::panel("info_only", {
            MenuNode::label("This panel is informational only."),
            MenuNode::separator(),
        }),
        MenuNode::interactive("last"),
    });

    std::vector<std::string> visited;
    collectInteractive(tree, visited);
    REQUIRE(visited.size() == 2);
    REQUIRE(visited[0] == "first");
    REQUIRE(visited[1] == "last");
}

TEST_CASE("reachability: room screen synthetic walk hits every actionable row",
          "[reachability][flat-menu][room]")
{
    /* Synthetic model of the post-fix CS Room screen. Members panel on
     * the left, settings on the right, both flattened panels under the
     * page root. Add Bot is the focus target after click; member rows
     * are interactive (selectable + context popup); the player count
     * header is a label (non-interactive). */
    auto tree = MenuNode::panel("room_page", {
        MenuNode::panel("left_player_panel", {
            MenuNode::label("Players in Room  (1 Player, 0/8 Bots)"),
            MenuNode::panel("members_list_scroll", {
                MenuNode::interactive("local_player_row"),
            }),
            MenuNode::interactive("add_bot_btn"),
            MenuNode::interactive("change_my_character_btn"),
        }),
        MenuNode::panel("right_settings_panel", {
            MenuNode::interactive("scenario"),
            MenuNode::interactive("arena"),
            MenuNode::interactive("time_limit"),
            MenuNode::interactive("score_limit"),
        }),
        MenuNode::interactive("start_match_action"),
        MenuNode::interactive("leave_room_action"),
    });

    std::vector<std::string> visited;
    collectInteractive(tree, visited);

    /* Every does-something option is reachable. */
    const std::vector<std::string> expected = {
        "local_player_row",
        "add_bot_btn",
        "change_my_character_btn",
        "scenario",
        "arena",
        "time_limit",
        "score_limit",
        "start_match_action",
        "leave_room_action",
    };
    REQUIRE(visited == expected);

    /* And no does-nothing option is reachable. */
    for (const auto &id : visited) {
        REQUIRE(id.find("Players in Room") == std::string::npos);
    }
}
