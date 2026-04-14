#!/usr/bin/env bash
# build-env.sh -- Source this before running ninja directly from bash.
# Sets TEMP/TMP to a writable directory and adds MinGW64 to PATH (idempotent).
#
# Usage: source devtools/build-env.sh && ninja -C Build pd pd-server
#
# Do not rediscover TEMP or PATH manually. Do not invent alternatives.
# This is the canonical bash-side build environment for Perfect Dark 2.

export TEMP="C:/Users/mikeh/AppData/Local/Temp"
export TMP="$TEMP"
[ -d "$TEMP" ] || mkdir -p "$TEMP"

case ":$PATH:" in
  *":/c/msys64/mingw64/bin:"*) ;;
  *) export PATH="/c/msys64/mingw64/bin:$PATH" ;;
esac

export CCACHE_SLOPPINESS="pch_defines,time_macros"
echo "Build env: TEMP=$TEMP | mingw64 on PATH | ccache sloppy"
