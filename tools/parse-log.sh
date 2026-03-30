#!/usr/bin/env bash
# tools/parse-log.sh — PD2 log parser for AI sessions and debugging
#
# Usage:
#   tools/parse-log.sh [options]
#
# Options:
#   --target client|server|both    Which log to parse (default: both)
#   --filter <pattern>             Grep pattern to filter lines
#   --head <N>                     Show first N matching lines
#   --tail <N>                     Show last N matching lines
#   --around <pattern> <N>         Show N lines of context around each match
#   --crash                        Shortcut: filter for crash/error indicators, tail 50
#   --warnings                     Shortcut: filter for warnings, tail 100
#   --net                          Shortcut: filter for network events, tail 100
#   --summary                      Print log metadata (size, line count, timestamps, error counts)
#   --path <dir>                   Override log directory (default: auto-detect from Build/)

set -euo pipefail

# Global temp file (cleaned up on exit and within each function call)
_TMPFILE=""
trap '[[ -n "$_TMPFILE" ]] && rm -f "$_TMPFILE"' EXIT

# ---------------------------------------------------------------------------
# Script location / project root
# ---------------------------------------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

# ---------------------------------------------------------------------------
# Defaults
# ---------------------------------------------------------------------------
TARGET="both"
FILTER=""
HEAD_N=""
TAIL_N=""
AROUND_PATTERN=""
AROUND_N=""
MODE="lines"    # lines | summary
OVERRIDE_PATH=""

# ---------------------------------------------------------------------------
# Argument parsing
# ---------------------------------------------------------------------------
while [[ $# -gt 0 ]]; do
    case "$1" in
        --target)
            TARGET="$2"; shift 2 ;;
        --filter)
            FILTER="$2"; shift 2 ;;
        --head)
            HEAD_N="$2"; shift 2 ;;
        --tail)
            TAIL_N="$2"; shift 2 ;;
        --around)
            AROUND_PATTERN="$2"; AROUND_N="$3"; shift 3 ;;
        --crash)
            FILTER="ERROR|FATAL|CRASH|assert|SIGSEGV|Access violation|exception|Segmentation fault"
            TAIL_N="50"
            shift ;;
        --warnings)
            FILTER="WARN|WARNING|warn"
            TAIL_N="100"
            shift ;;
        --net)
            FILTER="NET:|net:|connect|disconnect|ENet|STUN|punch|query|lobby|NAT"
            TAIL_N="100"
            shift ;;
        --summary)
            MODE="summary"
            shift ;;
        --path)
            OVERRIDE_PATH="$2"; shift 2 ;;
        --help|-h)
            sed -n '2,20p' "$0" | sed 's/^# \{0,1\}//'
            exit 0 ;;
        *)
            echo "ERROR: Unknown option: $1" >&2
            exit 1 ;;
    esac
done

# ---------------------------------------------------------------------------
# Validate target
# ---------------------------------------------------------------------------
case "$TARGET" in
    client|server|both) ;;
    *) echo "ERROR: --target must be client, server, or both" >&2; exit 1 ;;
esac

# ---------------------------------------------------------------------------
# Resolve log paths
# ---------------------------------------------------------------------------
find_log() {
    local name="$1"   # "client" or "server"
    local logfile=""

    if [[ -n "$OVERRIDE_PATH" ]]; then
        # User-specified directory
        if [[ "$name" == "client" ]]; then
            logfile="$OVERRIDE_PATH/pd-client.log"
        else
            logfile="$OVERRIDE_PATH/pd-server.log"
        fi
    else
        # Auto-detect: look in Build/<name>/ relative to project root
        local build_dir="$PROJECT_DIR/Build/$name"
        if [[ "$name" == "client" ]]; then
            logfile="$build_dir/pd-client.log"
        else
            logfile="$build_dir/pd-server.log"
        fi

        # Fallback: lowercase build dir
        if [[ ! -f "$logfile" ]]; then
            local build_dir_lc="$PROJECT_DIR/build/$name"
            if [[ "$name" == "client" ]]; then
                logfile="$build_dir_lc/pd-client.log"
            else
                logfile="$build_dir_lc/pd-server.log"
            fi
        fi
    fi

    echo "$logfile"
}

# ---------------------------------------------------------------------------
# Summary mode: print metadata for a single log file
# ---------------------------------------------------------------------------
print_summary() {
    local logfile="$1"
    local label="$2"

    echo "=== $label LOG SUMMARY ==="
    echo "  Path: $logfile"

    if [[ ! -f "$logfile" ]]; then
        echo "  [FILE NOT FOUND]"
        echo ""
        return
    fi

    local size
    size=$(wc -c < "$logfile" | tr -d '[:space:]')
    local lines
    lines=$(wc -l < "$logfile" | tr -d '[:space:]')
    local first_ts last_ts
    first_ts=$(grep -m1 '^\[' "$logfile" 2>/dev/null | grep -oE '^\[[0-9]+:[0-9]+\.[0-9]+\]' | head -1 || true)
    [[ -z "$first_ts" ]] && first_ts="(none)"
    last_ts=$(grep '^\[' "$logfile" 2>/dev/null | grep -oE '^\[[0-9]+:[0-9]+\.[0-9]+\]' | tail -1 || true)
    [[ -z "$last_ts" ]] && last_ts="(none)"

    local errors warns fatals
    errors=$(grep -cE 'ERROR' "$logfile" 2>/dev/null || true); errors="${errors:-0}"; errors=$(echo "$errors" | tr -d '[:space:]')
    warns=$(grep -cE 'WARN|WARNING' "$logfile" 2>/dev/null || true);   warns="${warns:-0}";   warns=$(echo "$warns" | tr -d '[:space:]')
    fatals=$(grep -cE 'FATAL|CRASH|assert|SIGSEGV' "$logfile" 2>/dev/null || true); fatals="${fatals:-0}"; fatals=$(echo "$fatals" | tr -d '[:space:]')

    printf "  Size:        %d bytes\n" "$size"
    printf "  Lines:       %d\n" "$lines"
    printf "  First ts:    %s\n" "$first_ts"
    printf "  Last ts:     %s\n" "$last_ts"
    printf "  ERROR lines: %d\n" "$errors"
    printf "  WARN lines:  %d\n" "$warns"
    printf "  FATAL/CRASH: %d\n" "$fatals"
    echo ""
}

# ---------------------------------------------------------------------------
# Lines mode: filter and paginate a single log file
# ---------------------------------------------------------------------------
process_log() {
    local logfile="$1"
    local label="$2"

    echo ""
    echo "========================================"
    echo "  $label LOG"
    echo "  $logfile"
    echo "========================================"

    if [[ ! -f "$logfile" ]]; then
        echo "  [FILE NOT FOUND]"
        return
    fi

    # --around mode: grep -n with context, no head/tail slicing
    if [[ -n "$AROUND_PATTERN" ]]; then
        echo "  (context: $AROUND_N lines around matches for: $AROUND_PATTERN)"
        echo ""
        grep -nE --context="$AROUND_N" "$AROUND_PATTERN" "$logfile" || echo "  (no matches)"
        return
    fi

    # Build a pipeline:
    #   cat logfile
    #   | [filter with grep -n]
    #   | [head or tail slice]
    #
    # We preserve line numbers by using grep -n when a filter is present,
    # or awk to prepend line numbers when no filter is given.

    # Use a global tmpfile so the EXIT trap (if inherited) can see it.
    # We clean up explicitly on every code path to avoid leaking temp files.
    _TMPFILE=$(mktemp /tmp/pd-parselog-XXXXXX)

    if [[ -n "$FILTER" ]]; then
        # grep -n: prefix each match with its original line number
        grep -nE "$FILTER" "$logfile" > "$_TMPFILE" 2>/dev/null || true
    else
        # No filter: number every line, then slice with head/tail
        awk '{printf "%d\t%s\n", NR, $0}' "$logfile" > "$_TMPFILE"
    fi

    local total
    total=$(wc -l < "$_TMPFILE" | tr -d '[:space:]')

    if [[ "$total" -eq 0 ]]; then
        rm -f "$_TMPFILE"
        echo "  (no matches)"
        return
    fi

    if [[ -n "$HEAD_N" && -n "$TAIL_N" ]]; then
        echo "  (showing first $HEAD_N and last $TAIL_N of $total matching lines)"
        echo ""
        echo "  --- first $HEAD_N ---"
        head -n "$HEAD_N" "$_TMPFILE"
        echo ""
        echo "  --- last $TAIL_N ---"
        tail -n "$TAIL_N" "$_TMPFILE"
    elif [[ -n "$HEAD_N" ]]; then
        echo "  (showing first $HEAD_N of $total matching lines)"
        echo ""
        head -n "$HEAD_N" "$_TMPFILE"
    elif [[ -n "$TAIL_N" ]]; then
        echo "  (showing last $TAIL_N of $total matching lines)"
        echo ""
        tail -n "$TAIL_N" "$_TMPFILE"
    else
        # No head/tail: if no filter was set we already defaulted elsewhere;
        # just show all matches (use head guard for safety on huge logs)
        local show_limit=200
        if [[ "$total" -gt "$show_limit" ]]; then
            echo "  (showing last $show_limit of $total matching lines -- use --head or --tail to control)"
            echo ""
            tail -n "$show_limit" "$_TMPFILE"
        else
            echo "  ($total matching lines)"
            echo ""
            cat "$_TMPFILE"
        fi
    fi

    rm -f "$_TMPFILE"
}

# ---------------------------------------------------------------------------
# Apply default: if no filter and no head/tail and not summary, use tail 50
# ---------------------------------------------------------------------------
if [[ "$MODE" == "lines" && -z "$FILTER" && -z "$HEAD_N" && -z "$TAIL_N" && -z "$AROUND_PATTERN" ]]; then
    TAIL_N="50"
fi

# ---------------------------------------------------------------------------
# Main dispatch
# ---------------------------------------------------------------------------
run_for() {
    local name="$1"
    local label
    label=$(echo "$name" | tr '[:lower:]' '[:upper:]')
    local logfile
    logfile=$(find_log "$name")

    if [[ "$MODE" == "summary" ]]; then
        print_summary "$logfile" "$label"
    else
        process_log "$logfile" "$label"
    fi
}

case "$TARGET" in
    client) run_for client ;;
    server) run_for server ;;
    both)
        run_for client
        run_for server
        ;;
esac
