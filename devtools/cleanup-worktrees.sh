#!/usr/bin/env bash
# cleanup-worktrees.sh -- Prune stale git worktrees and remove orphaned directories.
#
# Usage:  ./devtools/cleanup-worktrees.sh          (dry run -- reports only)
#         ./devtools/cleanup-worktrees.sh --delete  (actually removes orphaned dirs)
#
# Run from the repo root (perfect_dark-mike/).

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WORKTREE_DIR="$REPO_ROOT/.claude/worktrees"
DELETE_MODE=false

if [[ "${1:-}" == "--delete" ]]; then
    DELETE_MODE=true
fi

echo "=== Worktree Cleanup ==="
echo "Repo:       $REPO_ROOT"
echo "Worktrees:  $WORKTREE_DIR"
echo ""

# Step 1: git worktree prune (removes stale registry entries)
echo "[1/3] Pruning stale worktree registry entries..."
cd "$REPO_ROOT"
git worktree prune -v 2>&1 | while read -r line; do echo "  $line"; done
echo ""

# Step 2: Get registered vs on-disk lists
echo "[2/3] Comparing registered worktrees vs on-disk directories..."

registered=$(git worktree list --porcelain 2>/dev/null \
    | grep "^worktree " \
    | sed 's|^worktree ||' \
    | while read -r w; do basename "$w"; done \
    | sort)

on_disk=$(find "$WORKTREE_DIR" -mindepth 1 -maxdepth 1 -type d 2>/dev/null \
    | while read -r d; do basename "$d"; done \
    | sort)

orphaned=$(comm -23 <(echo "$on_disk") <(echo "$registered"))

if [[ -z "$orphaned" ]]; then
    echo "  No orphaned directories found."
else
    count=$(echo "$orphaned" | wc -l | tr -d ' ')
    echo "  Found $count orphaned directories:"
    echo "$orphaned" | while read -r name; do
        dir="$WORKTREE_DIR/$name"
        size=$(du -sh "$dir" 2>/dev/null | cut -f1)
        echo "    $name  ($size)"
    done
fi
echo ""

# Step 3: Delete orphans (if --delete)
echo "[3/3] Cleanup..."
if [[ -z "$orphaned" ]]; then
    echo "  Nothing to clean."
elif $DELETE_MODE; then
    echo "$orphaned" | while read -r name; do
        dir="$WORKTREE_DIR/$name"
        echo "  Removing $name ..."
        rm -rf "$dir"
    done
    # Also clean up any associated branches
    echo ""
    echo "  Cleaning orphaned claude/* branches..."
    echo "$orphaned" | while read -r name; do
        branch="claude/$name"
        if git show-ref --verify --quiet "refs/heads/$branch" 2>/dev/null; then
            git branch -D "$branch" 2>/dev/null && echo "    Deleted branch $branch" || true
        fi
    done
    echo ""
    echo "  Done. Cleaned $count orphaned worktree directories."
else
    echo "  Dry run -- pass --delete to actually remove orphaned directories."
fi

echo ""
echo "=== Summary ==="
remaining=$(find "$WORKTREE_DIR" -mindepth 1 -maxdepth 1 -type d 2>/dev/null | wc -l | tr -d ' ')
registered_count=$(git worktree list 2>/dev/null | wc -l | tr -d ' ')
echo "  Registered worktrees: $registered_count"
echo "  On-disk directories:  $remaining"
