#!/bin/bash
# git-snapshot.sh — Record working tree state before a risky git operation.
# Saves HEAD SHA, diff stats, and line counts to .claude/git-snapshots/.
# Usage: source devtools/git-snapshot.sh   (or bash devtools/git-snapshot.sh)
#
# The snapshot file is timestamped. git-verify-snapshot.sh compares against
# the most recent snapshot.

set -euo pipefail

REPO_ROOT="$(git rev-parse --show-toplevel)"
SNAP_DIR="${REPO_ROOT}/.claude/git-snapshots"
TIMESTAMP="$(date +%Y%m%d-%H%M%S)"
SNAP_FILE="${SNAP_DIR}/snapshot-${TIMESTAMP}.txt"

mkdir -p "${SNAP_DIR}"

{
    echo "=== Git Pre-Op Snapshot ==="
    echo "Timestamp: $(date -Iseconds)"
    echo "HEAD: $(git rev-parse HEAD)"
    echo "Branch: $(git branch --show-current)"
    echo ""

    echo "=== Staged Changes (diff --stat --cached) ==="
    git diff --stat --cached 2>/dev/null || echo "(none)"
    echo ""

    echo "=== Unstaged Changes (diff --stat) ==="
    git diff --stat 2>/dev/null || echo "(none)"
    echo ""

    echo "=== Line Counts of Changed Files ==="
    # Combine staged + unstaged changed files, deduplicate, count lines
    {
        git diff --cached --name-only 2>/dev/null
        git diff --name-only 2>/dev/null
    } | sort -u | while IFS= read -r f; do
        if [ -f "${REPO_ROOT}/${f}" ]; then
            lines=$(wc -l < "${REPO_ROOT}/${f}")
            echo "${lines} ${f}"
        else
            echo "MISSING ${f}"
        fi
    done
} > "${SNAP_FILE}"

echo "Snapshot saved: ${SNAP_FILE}"
