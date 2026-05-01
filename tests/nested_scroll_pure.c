/*
 * nested_scroll_pure.c -- Phase 2 fix #2 walker reference impl.
 * See nested_scroll_pure.h for contract.
 *
 * The production version at port/fast3d/pdgui_backend.cpp uses
 * ImGuiWindow* and consults Window->DC.ChildWindows / Window->ParentWindow
 * / Window->Active / Window->ScrollMax.y. Algorithm is identical.
 */

#include "nested_scroll_pure.h"
#include <stddef.h>

NspNode *nspFindInnermostScrollable(NspNode *root)
{
    if (!root || !root->active) {
        return NULL;
    }

    NspNode *best = (root->scroll_max_y > 0.0f) ? root : NULL;

    for (int i = 0; i < root->child_count; i++) {
        NspNode *child = root->children[i];
        if (!child || !child->active) {
            continue;
        }
        NspNode *deeper = nspFindInnermostScrollable(child);
        if (deeper) {
            best = deeper; /* prefer descendants over ancestors */
        }
    }

    return best;
}

NspNode *nspFindScrollableAncestor(NspNode *leaf)
{
    NspNode *w = leaf ? leaf->parent : NULL;
    while (w) {
        if (w->active && w->scroll_max_y > 0.0f) {
            return w;
        }
        w = w->parent;
    }
    return NULL;
}

NspNode *nspChooseScrollTarget(NspNode *nav_root)
{
    if (!nav_root) {
        return NULL;
    }

    /* Prefer deepest visible scrollable descendant (including self). */
    NspNode *target = nspFindInnermostScrollable(nav_root);
    if (target) {
        return target;
    }

    /* Fallback: first scrollable ancestor (covers cases where nav has
     * settled on a non-scrollable leaf inside a scrollable wrapper,
     * e.g. a button directly inside an outer non-flattened scroll).
     *
     * Note: this branch is rare in the PD2 codebase because NavFlattened
     * is dominant -- NavWindow normally points at the outer root, which
     * IS the ancestor walk's starting point. Kept for correctness in
     * cases where NavWindow is set to a non-flattened leaf. */
    return nspFindScrollableAncestor(nav_root);
}
