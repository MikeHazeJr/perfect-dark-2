#!/bin/bash
# git-verify-snapshot.sh — Compare current state against the last snapshot.
# Exits non-zero if any tracked file shrank (line count decreased).
# Usage: bash devtools/git-verify-snapshot.sh
#
# Accepts optional argument: path to a specific snapshot file.
# Without arguments, uses the most recent snapshot in .claude/git-snapshots/.

set -euo pipefail

REPO_ROOT="$(git rev-parse --show-toplevel)"
SNAP_DIR="${REPO_ROOT}/.claude/git-snapshots"

if [ $# -ge 1 ]; then
    SNAP_FILE="$1"
else
    SNAP_FILE="$(ls -t "${SNAP_DIR}"/snapshot-*.txt 2>/dev/null | head -1)"
fi

if [ -z "${SNAP_FILE}" ] || [ ! -f "${SNAP_FILE}" ]; then
    echo "ERROR: No snapshot found. Run devtools/git-snapshot.sh first."
    exit 2
fi

echo "Verifying against: ${SNAP_FILE}"
echo ""

# Extract the HEAD from snapshot
SNAP_HEAD=$(grep '^HEAD:' "${SNAP_FILE}" | awk '{print $2}')
CURR_HEAD=$(git rev-parse HEAD)
echo "Snapshot HEAD: ${SNAP_HEAD}"
echo "Current HEAD:  ${CURR_HEAD}"
echo ""

# Parse line counts section and compare
FAILURES=0
IN_SECTION=0

while IFS= read -r line; do
    if [[ "${line}" == "=== Line Counts of Changed Files ===" ]]; then
        IN_SECTION=1
        continue
    fi
    if [[ ${IN_SECTION} -eq 0 ]]; then
        continue
    fi
    # Skip empty lines and section headers
    [[ -z "${line}" ]] && continue
    [[ "${line}" == "==="* ]] && break

    # Parse "LINECOUNT filepath" or "MISSING filepath"
    SNAP_COUNT=$(echo "${line}" | awk '{print $1}')
    FILEPATH=$(echo "${line}" | awk '{$1=""; print $0}' | sed 's/^ //')

    if [ "${SNAP_COUNT}" = "MISSING" ]; then
        continue
    fi

    FULL_PATH="${REPO_ROOT}/${FILEPATH}"
    if [ ! -f "${FULL_PATH}" ]; then
        echo "WARN: File disappeared: ${FILEPATH} (was ${SNAP_COUNT} lines)"
        FAILURES=$((FAILURES + 1))
        continue
    fi

    CURR_COUNT=$(wc -l < "${FULL_PATH}")
    if [ "${CURR_COUNT}" -lt "${SNAP_COUNT}" ]; then
        DELTA=$((SNAP_COUNT - CURR_COUNT))
        echo "SHRINK: ${FILEPATH}: ${SNAP_COUNT} -> ${CURR_COUNT} (-${DELTA} lines)"
        FAILURES=$((FAILURES + 1))
    fi
done < "${SNAP_FILE}"

echo ""
if [ ${FAILURES} -gt 0 ]; then
    echo "FAIL: ${FAILURES} file(s) shrank or disappeared. Investigate before continuing."
    exit 1
else
    echo "OK: No files shrank."
    exit 0
fi
