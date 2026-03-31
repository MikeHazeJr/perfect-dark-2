#!/usr/bin/env bash
# check-worktree.sh — Worktree guard for AI code tasks.
#
# Run this at the start of any code task to verify you're in the main
# working copy and not accidentally inside a .claude/worktrees/ path.
#
# Usage:
#   bash devtools/check-worktree.sh
#   source devtools/check-worktree.sh   # exits the calling shell on failure
#
# Exit codes:
#   0 — safe, working in main copy
#   1 — inside a worktree, task should abort

MAIN_COPY="C:/Users/mikeh/Perfect-Dark-2/perfect_dark-mike"

# Normalize to forward slashes
CURRENT_DIR="$(pwd | sed 's|\\|/|g')"

if echo "$CURRENT_DIR" | grep -q "\.claude/worktrees"; then
    echo ""
    echo "╔══════════════════════════════════════════════════════════════════╗"
    echo "║           [WORKTREE-GUARD] WRONG DIRECTORY DETECTED             ║"
    echo "╠══════════════════════════════════════════════════════════════════╣"
    echo "║                                                                  ║"
    echo "║  This session is running inside a worktree:                     ║"
    echo "║    $CURRENT_DIR"
    echo "║                                                                  ║"
    echo "║  Worktrees are for planning/research only.                       ║"
    echo "║  Code tasks MUST run in the main working copy:                  ║"
    echo "║    C:\\Users\\mikeh\\Perfect-Dark-2\\perfect_dark-mike             ║"
    echo "║                                                                  ║"
    echo "║  ACTION REQUIRED: Switch to the main copy before proceeding.    ║"
    echo "║                                                                  ║"
    echo "╚══════════════════════════════════════════════════════════════════╝"
    echo ""
    exit 1
fi

echo "[WORKTREE-GUARD] OK — running in main copy: $CURRENT_DIR"
exit 0
