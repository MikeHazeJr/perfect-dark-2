#!/usr/bin/env bash
# tools/build.sh — Clean build helper for Perfect Dark PC Port
#
# Every invocation creates a unique temp build directory (build_test_<pid>_<ts>/).
# On success: prints BUILD OK and removes the build dir.
# On failure: prints error summary + log path and preserves the build dir.
#
# Usage:
#   tools/build.sh [--target client|server|both] [--source-dir <path>]
#
# Exit codes:
#   0 = success
#   1 = client build failed
#   2 = server build failed
#   3 = both failed
#   4 = environment error (MSYS2/cmake/make/cc not found)

# ---------------------------------------------------------------------------
# Environment — set TEMP before any subprocess launches
# ---------------------------------------------------------------------------
export TEMP=/tmp
export TMP=/tmp
export MSYSTEM=MINGW64
export MINGW_PREFIX=/mingw64

# Ensure MSYS2/MINGW64 bin dirs are in PATH
if ! echo "$PATH" | grep -q "/mingw64/bin"; then
    export PATH="/mingw64/bin:/usr/bin:$PATH"
fi

# ---------------------------------------------------------------------------
# Argument parsing
# ---------------------------------------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SOURCE_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
TARGET="both"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --target)
            if [[ -z "${2:-}" ]]; then
                echo "ERROR: --target requires an argument (client|server|both)" >&2
                exit 4
            fi
            TARGET="$2"
            shift 2
            ;;
        --source-dir)
            if [[ -z "${2:-}" ]]; then
                echo "ERROR: --source-dir requires a path argument" >&2
                exit 4
            fi
            SOURCE_DIR="$2"
            shift 2
            ;;
        -h|--help)
            echo "Usage: $0 [--target client|server|both] [--source-dir <path>]"
            echo ""
            echo "Options:"
            echo "  --target      What to build: client, server, or both (default: both)"
            echo "  --source-dir  Project root containing CMakeLists.txt (default: parent of tools/)"
            exit 0
            ;;
        *)
            echo "ERROR: Unknown argument: $1" >&2
            exit 4
            ;;
    esac
done

case "$TARGET" in
    client|server|both) ;;
    *)
        echo "ERROR: Invalid target '$TARGET'. Must be: client, server, or both." >&2
        exit 4
        ;;
esac

# ---------------------------------------------------------------------------
# Environment validation — find required tools
# ---------------------------------------------------------------------------
find_tool() {
    local name="$1"; shift
    local found
    found="$(command -v "$name" 2>/dev/null)"
    if [[ -n "$found" ]]; then echo "$found"; return 0; fi
    for p in "$@"; do
        if [[ -x "$p" ]]; then echo "$p"; return 0; fi
    done
    return 1
}

CMAKE_EXE="$(find_tool cmake \
    /mingw64/bin/cmake.exe \
    /c/msys64/mingw64/bin/cmake.exe)" || {
    echo "ERROR: cmake not found." >&2
    echo "       Install: pacman -S mingw-w64-x86_64-cmake" >&2
    exit 4
}
MAKE_EXE="$(find_tool make \
    /usr/bin/make.exe \
    /c/msys64/usr/bin/make.exe)" || {
    echo "ERROR: make not found." >&2
    echo "       Install: pacman -S make" >&2
    exit 4
}
CC_EXE="$(find_tool cc \
    /mingw64/bin/cc.exe \
    /c/msys64/mingw64/bin/cc.exe)" || {
    echo "ERROR: cc not found." >&2
    echo "       Install: pacman -S mingw-w64-x86_64-gcc" >&2
    exit 4
}

# CMake is a native Windows application — it requires Windows-format paths.
# cygpath -m converts MSYS2 paths to mixed Windows format (C:/msys64/...).
to_win() { cygpath -m "$1" 2>/dev/null || echo "$1"; }
CMAKE_WIN="$(to_win "$CMAKE_EXE")"
MAKE_WIN="$(to_win "$MAKE_EXE")"
CC_WIN="$(to_win "$CC_EXE")"
SOURCE_WIN="$(to_win "$SOURCE_DIR")"

if [[ ! -f "$SOURCE_DIR/CMakeLists.txt" ]]; then
    echo "ERROR: CMakeLists.txt not found in source dir: $SOURCE_DIR" >&2
    exit 4
fi

# ---------------------------------------------------------------------------
# Build directory — unique per invocation, nested under ClaudeBuilds/
# ---------------------------------------------------------------------------
BUILD_ID="build_test_$$_$(date +%Y%m%d_%H%M%S)"
CLAUDE_BUILDS_DIR="$SOURCE_DIR/ClaudeBuilds"
BUILD_DIR="$CLAUDE_BUILDS_DIR/$BUILD_ID"
LOG_FILE="$BUILD_DIR/build.log"

mkdir -p "$BUILD_DIR"

TOTAL_START=$(date +%s)

echo "============================================================"
echo "  Perfect Dark PC Port — Clean Build"
echo "  Target:     $TARGET"
echo "  Source dir: $SOURCE_DIR"
echo "  Build dir:  $BUILD_DIR"
echo "  cmake:      $CMAKE_WIN"
echo "  make:       $MAKE_WIN"
echo "  cc:         $CC_WIN"
echo "============================================================"
echo ""

# Log header
{
    echo "Perfect Dark PC Port — build log"
    echo "Date:       $(date)"
    echo "Target:     $TARGET"
    echo "Source dir: $SOURCE_DIR"
    echo "Build dir:  $BUILD_DIR"
    echo "cmake:      $CMAKE_WIN"
    echo "make:       $MAKE_WIN"
    echo "cc:         $CC_WIN"
    echo "================================================================"
    echo ""
} >> "$LOG_FILE"

# ---------------------------------------------------------------------------
# Helper: extract first real compiler/linker error from log
# ---------------------------------------------------------------------------
extract_first_error() {
    grep -m1 -E \
        '(:[0-9]+:[0-9]+: (error|fatal error):|: error: | error: |undefined reference to|cannot find -l|CMake Error|ld returned [0-9]|collect2: error)' \
        "$1" 2>/dev/null \
        | sed 's|^[[:space:]]*||' \
        || echo "(no recognizable error line — see full log)"
}

# ---------------------------------------------------------------------------
# Phase 1: CMake Configure (once, covers both client and server targets)
# NOTE: PD_SERVER=1 is a target_compile_definitions on pd-server in CMakeLists.txt
#       — no separate configure needed for the server target.
# ---------------------------------------------------------------------------
BUILD_WIN="$(to_win "$BUILD_DIR")"

echo "  [configure] cmake -G \"Unix Makefiles\" ..."
{
    echo "[CONFIGURE] $(date)"
    echo "Command: $CMAKE_WIN -G \"Unix Makefiles\" \\"
    echo "    -DCMAKE_MAKE_PROGRAM=\"$MAKE_WIN\" \\"
    echo "    -DCMAKE_C_COMPILER=\"$CC_WIN\" \\"
    echo "    -B \"$BUILD_WIN\" -S \"$SOURCE_WIN\""
    echo ""
} >> "$LOG_FILE"

T_CONF_START=$(date +%s)

"$CMAKE_EXE" \
    -G "Unix Makefiles" \
    -DCMAKE_MAKE_PROGRAM="$MAKE_WIN" \
    -DCMAKE_C_COMPILER="$CC_WIN" \
    -B "$BUILD_WIN" \
    -S "$SOURCE_WIN" \
    >> "$LOG_FILE" 2>&1
CONF_RC=$?

T_CONF_END=$(date +%s)
T_CONF=$(( T_CONF_END - T_CONF_START ))

if [[ $CONF_RC -ne 0 ]]; then
    echo ""
    echo "  CONFIGURE FAILED (exit $CONF_RC, ${T_CONF}s)"
    echo ""
    FIRST_ERR="$(extract_first_error "$LOG_FILE")"
    echo "  First error: $FIRST_ERR"
    echo ""
    echo "  Full log: $LOG_FILE"
    echo "  Build dir preserved: $BUILD_DIR"
    echo ""
    case "$TARGET" in
        client) exit 1 ;;
        server) exit 2 ;;
        both)   exit 3 ;;
    esac
fi

echo "  configure OK (${T_CONF}s)"
echo ""

# ---------------------------------------------------------------------------
# Phase 2: Build targets
# ---------------------------------------------------------------------------
NPROC=$(nproc 2>/dev/null || echo "4")

CLIENT_OK=0
SERVER_OK=0
CLIENT_ELAPSED=0
SERVER_ELAPSED=0

# build_one <label> <cmake-target>
# Sets CLIENT_OK/SERVER_OK and CLIENT_ELAPSED/SERVER_ELAPSED.
build_one() {
    local label="$1"
    local cmake_target="$2"

    echo "  [build] $label (--target $cmake_target, -j$NPROC)..."

    {
        echo ""
        echo "[BUILD:$label] $(date)"
        echo "Command: $CMAKE_WIN --build \"$BUILD_WIN\" --target $cmake_target -- -j$NPROC"
        echo ""
    } >> "$LOG_FILE"

    local t_start
    t_start=$(date +%s)

    "$CMAKE_EXE" --build "$BUILD_WIN" \
        --target "$cmake_target" \
        -- -j"$NPROC" \
        >> "$LOG_FILE" 2>&1
    local rc=$?

    local t_end
    t_end=$(date +%s)
    local elapsed=$(( t_end - t_start ))

    if [[ "$label" == "client" ]]; then
        CLIENT_ELAPSED=$elapsed
    else
        SERVER_ELAPSED=$elapsed
    fi

    if [[ $rc -eq 0 ]]; then
        echo "  BUILD OK: $label (${elapsed}s)"
        if [[ "$label" == "client" ]]; then
            CLIENT_OK=1
        else
            SERVER_OK=1
        fi
        return 0
    else
        echo ""
        echo "  BUILD FAILED: $label (exit $rc, ${elapsed}s)"
        local first_err
        first_err="$(extract_first_error "$LOG_FILE")"
        echo "  First error: $first_err"
        echo "  Full log:    $LOG_FILE"
        echo ""
        return 1
    fi
}

case "$TARGET" in
    client)
        build_one "client" "pd"
        ;;
    server)
        build_one "server" "pd-server"
        ;;
    both)
        build_one "client" "pd"
        build_one "server" "pd-server"
        ;;
esac

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------
TOTAL_END=$(date +%s)
TOTAL_ELAPSED=$(( TOTAL_END - TOTAL_START ))

echo ""
echo "============================================================"
echo "  BUILD SUMMARY"
echo "============================================================"

ALL_OK=1
EXIT_CODE=0

case "$TARGET" in
    client)
        if [[ $CLIENT_OK -eq 1 ]]; then
            echo "  [PASS]  client   (${CLIENT_ELAPSED}s)"
        else
            echo "  [FAIL]  client   (${CLIENT_ELAPSED}s)"
            ALL_OK=0
            EXIT_CODE=1
        fi
        ;;
    server)
        if [[ $SERVER_OK -eq 1 ]]; then
            echo "  [PASS]  server   (${SERVER_ELAPSED}s)"
        else
            echo "  [FAIL]  server   (${SERVER_ELAPSED}s)"
            ALL_OK=0
            EXIT_CODE=2
        fi
        ;;
    both)
        if [[ $CLIENT_OK -eq 1 ]]; then
            echo "  [PASS]  client   (${CLIENT_ELAPSED}s)"
        else
            echo "  [FAIL]  client   (${CLIENT_ELAPSED}s)"
            ALL_OK=0
        fi
        if [[ $SERVER_OK -eq 1 ]]; then
            echo "  [PASS]  server   (${SERVER_ELAPSED}s)"
        else
            echo "  [FAIL]  server   (${SERVER_ELAPSED}s)"
            ALL_OK=0
        fi
        if [[ $ALL_OK -eq 0 ]]; then
            if [[ $CLIENT_OK -eq 0 && $SERVER_OK -eq 0 ]]; then
                EXIT_CODE=3
            elif [[ $CLIENT_OK -eq 0 ]]; then
                EXIT_CODE=1
            else
                EXIT_CODE=2
            fi
        fi
        ;;
esac

echo "  Total time: ${TOTAL_ELAPSED}s"
echo "============================================================"

if [[ $ALL_OK -eq 1 ]]; then
    echo "  Result: SUCCESS"
    echo ""
    echo "  Cleaning up build directory..."
    rm -rf "$BUILD_DIR"
    echo "  Cleaned: $BUILD_DIR"
    rmdir "$CLAUDE_BUILDS_DIR" 2>/dev/null && echo "  Removed empty ClaudeBuilds/" || true
else
    echo "  Result: FAILED"
    echo "  Build dir preserved: $BUILD_DIR"
    echo "  Full log: $LOG_FILE"
fi

echo "============================================================"

exit $EXIT_CODE
