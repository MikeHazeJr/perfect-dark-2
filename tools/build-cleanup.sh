#!/usr/bin/env bash
# tools/build-cleanup.sh — Remove leftover build_test_* directories
#
# Failed builds preserve their build directory so you can inspect the log.
# Run this script periodically to clean them up.
#
# Usage:
#   tools/build-cleanup.sh [--source-dir <path>]
#   tools/build-cleanup.sh --dry-run

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SOURCE_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
CLAUDE_BUILDS_DIR="$SOURCE_DIR/ClaudeBuilds"
DRY_RUN=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --source-dir)
            SOURCE_DIR="${2:?'--source-dir requires a path'}"
            CLAUDE_BUILDS_DIR="$SOURCE_DIR/ClaudeBuilds"
            shift 2
            ;;
        --dry-run|-n)
            DRY_RUN=1
            shift
            ;;
        -h|--help)
            echo "Usage: $0 [--source-dir <path>] [--dry-run]"
            echo ""
            echo "Finds and removes all build_test_* directories left by failed builds."
            echo ""
            echo "Options:"
            echo "  --source-dir  Project root to search (default: parent of tools/)"
            echo "  --dry-run     Show what would be deleted without deleting"
            exit 0
            ;;
        *)
            echo "ERROR: Unknown argument: $1" >&2
            exit 1
            ;;
    esac
done

echo "Scanning: $CLAUDE_BUILDS_DIR"
if [[ $DRY_RUN -eq 1 ]]; then
    echo "(dry run — nothing will be deleted)"
fi
echo ""

FOUND=0
TOTAL_SIZE=0

while IFS= read -r -d '' dir; do
    FOUND=$(( FOUND + 1 ))
    # Estimate size (du -sh, suppress errors if du not available)
    SIZE="$(du -sh "$dir" 2>/dev/null | cut -f1)"
    SIZE="${SIZE:-?}"

    if [[ $DRY_RUN -eq 1 ]]; then
        echo "  Would delete: $dir  ($SIZE)"
    else
        echo "  Deleting: $dir  ($SIZE)"
        rm -rf "$dir"
    fi
done < <(find "$CLAUDE_BUILDS_DIR" -maxdepth 1 -type d -name "build_test_*" -print0 2>/dev/null)

echo ""
if [[ $FOUND -eq 0 ]]; then
    echo "No build_test_* directories found. Nothing to clean."
elif [[ $DRY_RUN -eq 1 ]]; then
    echo "Would delete $FOUND director$([ $FOUND -eq 1 ] && echo y || echo ies)."
else
    echo "Deleted $FOUND director$([ $FOUND -eq 1 ] && echo y || echo ies)."
    rmdir "$CLAUDE_BUILDS_DIR" 2>/dev/null && echo "Removed empty ClaudeBuilds/" || true
fi
