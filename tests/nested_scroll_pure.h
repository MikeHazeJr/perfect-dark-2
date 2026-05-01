/*
 * nested_scroll_pure.h -- Phase 2 fix #2 algorithm spec.
 *
 * Mike's directive (2026-05-01): "Right stick up and down will scroll
 * the deepest scrollbox it is in (if they are nested, and I am on a
 * button inside a scrollbox that is inside another scrollbox, the
 * innermost scrollbox should scroll)."
 *
 * Today the right-stick scroll path at port/fast3d/pdgui_backend.cpp:528
 * scrolls ctx->NavWindow only. Under ImGuiChildFlags_NavFlattened
 * (used pervasively in this codebase for cross-panel nav) NavWindow
 * collapses to the OUTER root, not the innermost scrollable child --
 * so the inner scrollbox never receives the delta.
 *
 * This pure-C module specifies the walker in abstract terms:
 *   - Each Node has zero or more children, a scroll_max_y, and an
 *     active flag.
 *   - nspFindInnermostScrollable(root) walks DOWN through visible
 *     descendants (depth-first) and returns the deepest visible
 *     scrollable node found in the subtree.
 *   - Falls back to walking UP via parent pointer if no descendant
 *     is scrollable (covers the case where NavWindow is itself the
 *     innermost scrollable, e.g. Theme Editor PaletteScroll).
 *
 * The production helper at port/fast3d/pdgui_backend.cpp mirrors this
 * algorithm in ImGuiWindow* terms.
 *
 * @SYNC port/fast3d/pdgui_backend.cpp::pdguiInnermostScrollableForNav
 * @SYNC context/audits/input-menu-system-audit-2026-05-01.md Section D
 */

#ifndef _IN_NESTED_SCROLL_PURE_H
#define _IN_NESTED_SCROLL_PURE_H

#ifdef __cplusplus
extern "C" {
#endif

#define NSP_MAX_CHILDREN 8

typedef struct NspNode {
    const char       *name;          /* for log readability */
    int               active;        /* 0 = not visible (skipped) */
    float             scroll_max_y;  /* > 0 means scrollable */
    struct NspNode   *parent;        /* upward link for fallback walk */
    struct NspNode   *children[NSP_MAX_CHILDREN];
    int               child_count;
} NspNode;

/* Construct: zero-initialise then set fields. */

/* Walks DOWN from root depth-first. Returns the deepest visible
 * scrollable descendant (including root itself when root is scrollable
 * and has no scrollable descendants). Returns NULL when no visible
 * scrollable node is found in the subtree. */
NspNode *nspFindInnermostScrollable(NspNode *root);

/* Walks UP from leaf via parent pointers looking for the first
 * scrollable ancestor. Returns NULL when no ancestor is scrollable. */
NspNode *nspFindScrollableAncestor(NspNode *leaf);

/* Combined walker: prefer the deepest visible scrollable descendant
 * of nav_root; if none, fall back to the first scrollable ancestor
 * of nav_root. Returns NULL when neither yields a result. This is
 * the function the right-stick handler calls. */
NspNode *nspChooseScrollTarget(NspNode *nav_root);

#ifdef __cplusplus
}
#endif

#endif /* _IN_NESTED_SCROLL_PURE_H */
