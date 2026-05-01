/*
 * test_nested_scroll.cpp -- Phase 2 fix #2 invariants.
 *
 * Locks down the "innermost scrollable descendant" walker that
 * port/fast3d/pdgui_backend.cpp uses to pick the right-stick scroll
 * target.
 *
 * Synthetic trees mirror the menu structures the audit identified:
 *   - Settings tabs: outer ##main_settings_body (NavFlattened,
 *     non-scrollable) wrapping per-tab ##settings_scroll_* (NavFlattened,
 *     scrollable). Pre-fix: outer received the delta and nothing
 *     visible scrolled. Post-fix: inner scrollable receives the delta.
 *   - Mod Manager: outer ##modmgr_inner wrapping ##modmgr_list +
 *     ##modmgr_details (both scrollable, side-by-side). Last-in-stack
 *     rule picks the most-recently-rendered child.
 *   - Theme Editor: outer non-NavFlattened modal with PaletteScroll
 *     direct child (only the inner is scrollable). Walker still
 *     finds the inner.
 *
 * @SYNC port/fast3d/pdgui_backend.cpp::pdguiInnermostScrollableForNav
 * @SYNC context/audits/input-menu-system-audit-2026-05-01.md Section D
 *
 * Logging channel reserved for runtime diagnostics: INPUT.SCROLL.NEST
 */

#include "catch.hpp"

extern "C" {
#include "nested_scroll_pure.h"
}

#include <cstring>

namespace {

NspNode mkNode(const char *name, int active, float scroll_max_y)
{
    NspNode n;
    std::memset(&n, 0, sizeof(n));
    n.name         = name;
    n.active       = active;
    n.scroll_max_y = scroll_max_y;
    return n;
}

void link(NspNode *parent, NspNode *child)
{
    REQUIRE(parent != nullptr);
    REQUIRE(child  != nullptr);
    REQUIRE(parent->child_count < NSP_MAX_CHILDREN);
    parent->children[parent->child_count++] = child;
    child->parent = parent;
}

} /* namespace */

TEST_CASE("nested-scroll: empty tree returns NULL", "[nested-scroll][safety]")
{
    REQUIRE(nspFindInnermostScrollable(nullptr) == nullptr);
    REQUIRE(nspChooseScrollTarget(nullptr)      == nullptr);
}

TEST_CASE("nested-scroll: lone scrollable leaf returns itself", "[nested-scroll][basic]")
{
    NspNode w = mkNode("solo", 1, 100.0f);
    REQUIRE(nspChooseScrollTarget(&w) == &w);
}

TEST_CASE("nested-scroll: lone non-scrollable leaf returns NULL", "[nested-scroll][basic]")
{
    NspNode w = mkNode("idle", 1, 0.0f);
    REQUIRE(nspChooseScrollTarget(&w) == nullptr);
}

TEST_CASE("nested-scroll: inactive scrollable returns NULL", "[nested-scroll][basic]")
{
    NspNode w = mkNode("hidden", 0, 100.0f);
    REQUIRE(nspChooseScrollTarget(&w) == nullptr);
}

TEST_CASE("nested-scroll: Settings tabs pattern picks inner scroll over outer wrapper", "[nested-scroll][settings]")
{
    /* outer body (NavFlattened, non-scrollable) wraps the per-tab
     * scrollable inner. Pre-fix the delta hit outer and nothing
     * happened. Post-fix the inner receives the delta. */
    NspNode outer = mkNode("##main_settings_body", 1, 0.0f);
    NspNode inner = mkNode("##settings_scroll_v",  1, 500.0f);
    link(&outer, &inner);

    NspNode *target = nspChooseScrollTarget(&outer);
    REQUIRE(target == &inner);
}

TEST_CASE("nested-scroll: three-level deep pick goes to deepest scrollable", "[nested-scroll][deep]")
{
    /* root -> mid (scrollable) -> leaf (scrollable). Deepest wins. */
    NspNode root = mkNode("root", 1, 0.0f);
    NspNode mid  = mkNode("mid",  1, 200.0f);
    NspNode leaf = mkNode("leaf", 1, 600.0f);
    link(&root, &mid);
    link(&mid,  &leaf);

    REQUIRE(nspChooseScrollTarget(&root) == &leaf);
    REQUIRE(nspChooseScrollTarget(&mid)  == &leaf);
    REQUIRE(nspChooseScrollTarget(&leaf) == &leaf);
}

TEST_CASE("nested-scroll: skips inactive children", "[nested-scroll][active]")
{
    /* outer has two children; one is inactive. The walker must skip
     * the inactive one and use only the active scrollable. */
    NspNode outer    = mkNode("outer",    1, 0.0f);
    NspNode hidden   = mkNode("hidden",   0, 700.0f); /* not visible */
    NspNode visible  = mkNode("visible",  1, 300.0f);
    link(&outer, &hidden);
    link(&outer, &visible);

    REQUIRE(nspChooseScrollTarget(&outer) == &visible);
}

TEST_CASE("nested-scroll: ModMgr two-column pattern picks last-in-stack scrollable", "[nested-scroll][modmgr]")
{
    /* modmgr layout: outer wraps list (left) + details (right), both
     * scrollable. Last-rendered wins. This is the pragmatic choice
     * for the deepest-descendant walker (multi-sibling case is
     * order-dependent; ImGui DC.ChildWindows reflects begin order). */
    NspNode outer   = mkNode("##modmgr_inner",  1, 0.0f);
    NspNode list    = mkNode("##modmgr_list",   1, 800.0f);
    NspNode details = mkNode("##modmgr_details",1, 600.0f);
    link(&outer, &list);
    link(&outer, &details);

    /* Walker yields the LAST-in-stack scrollable found (details). */
    REQUIRE(nspChooseScrollTarget(&outer) == &details);
}

TEST_CASE("nested-scroll: Theme Editor pattern picks PaletteScroll over preview", "[nested-scroll][theme]")
{
    /* Theme Editor modal: preview (non-scrollable) sibling of palette
     * (scrollable). NavWindow points at outer modal. Walker dives
     * into children, finds palette as the only scrollable, returns it. */
    NspNode modal   = mkNode("##theme_editor",  1, 0.0f);
    NspNode preview = mkNode("##theme_preview", 1, 0.0f);
    NspNode palette = mkNode("PaletteScroll",   1, 400.0f);
    link(&modal, &preview);
    link(&modal, &palette);

    REQUIRE(nspChooseScrollTarget(&modal) == &palette);
}

TEST_CASE("nested-scroll: descendant preferred over ancestor (no false-up walk)", "[nested-scroll][priority]")
{
    /* If both an ancestor AND a descendant are scrollable, descendant
     * wins (Mike's spec: "innermost"). Synthetic tree: scrollable
     * grand-parent contains a non-scrollable wrapper containing a
     * scrollable inner. Starting walk at the wrapper -- since the
     * wrapper itself has a scrollable child, we never need the
     * ancestor fallback. */
    NspNode grand   = mkNode("grand",   1, 100.0f); /* scrollable */
    NspNode wrapper = mkNode("wrapper", 1, 0.0f);
    NspNode inner   = mkNode("inner",   1, 700.0f); /* scrollable, deeper */
    link(&grand,   &wrapper);
    link(&wrapper, &inner);

    /* Starting from wrapper, descendant inner is preferred over the
     * ancestor grand. */
    REQUIRE(nspChooseScrollTarget(&wrapper) == &inner);
}

TEST_CASE("nested-scroll: ancestor fallback when subtree has no scrollable", "[nested-scroll][fallback]")
{
    /* If nav_root and ALL descendants are non-scrollable, fall back
     * to the first scrollable ancestor. */
    NspNode grand   = mkNode("grand",   1, 100.0f); /* scrollable */
    NspNode mid     = mkNode("mid",     1, 0.0f);
    NspNode leaf    = mkNode("leaf",    1, 0.0f);
    link(&grand, &mid);
    link(&mid,   &leaf);

    /* Starting walk at mid: mid is non-scrollable, leaf is non-scrollable.
     * Walker falls back to ancestor walk: grand is scrollable. */
    REQUIRE(nspChooseScrollTarget(&mid)  == &grand);
    REQUIRE(nspChooseScrollTarget(&leaf) == &grand);
}

TEST_CASE("nested-scroll: deeply nested with mixed actives picks the deepest visible", "[nested-scroll][stress]")
{
    /* Real-world case: outer wraps two levels of NavFlattened wrappers
     * with the actual scroll on a leaf. Tests recursion depth + skip-
     * inactive logic together. */
    NspNode outer        = mkNode("outer",        1, 0.0f);
    NspNode l1_wrapper   = mkNode("l1_wrapper",   1, 0.0f);
    NspNode l1_other_inactive = mkNode("inactive_branch", 0, 999.0f); /* skipped */
    NspNode l2_scroll    = mkNode("l2_scroll",    1, 250.0f);
    NspNode l2_dead      = mkNode("l2_dead",      1, 0.0f);
    NspNode l3_inner_scroll = mkNode("l3_inner_scroll", 1, 1200.0f); /* deepest */

    link(&outer,      &l1_wrapper);
    link(&outer,      &l1_other_inactive);
    link(&l1_wrapper, &l2_scroll);
    link(&l1_wrapper, &l2_dead);
    link(&l2_scroll,  &l3_inner_scroll);

    /* Deepest scrollable visible descendant: l3_inner_scroll. */
    REQUIRE(nspChooseScrollTarget(&outer) == &l3_inner_scroll);
}
