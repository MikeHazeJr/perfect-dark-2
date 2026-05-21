#!/usr/bin/env bash
# build-env.sh -- Source this before running ninja directly from bash.
# Sets TEMP/TMP to a writable directory and adds MinGW64 to PATH (idempotent).
#
# Usage: source devtools/build-env.sh && ninja -C Build pd pd-tests
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

# ccache needs USERPROFILE/LOCALAPPDATA/APPDATA/HOME present to locate the
# Windows-side cache directory. When bash is launched from a subshell that
# dropped these (some PS-from-bash and bat-wrapper paths), ccache silently
# swallows compiler stderr -- builds fail with no diagnostic. Defensive
# re-exports below; these are no-ops in a normal interactive shell where
# the variables already exist.
[ -z "$HOME" ] && export HOME="/c/Users/mikeh"
[ -z "$USERPROFILE" ] && export USERPROFILE="C:/Users/mikeh"
[ -z "$LOCALAPPDATA" ] && export LOCALAPPDATA="$USERPROFILE/AppData/Local"
[ -z "$APPDATA" ] && export APPDATA="$USERPROFILE/AppData/Roaming"

export CCACHE_SLOPPINESS="pch_defines,time_macros,include_file_mtime,include_file_ctime"
export CCACHE_BASEDIR="$(cd "${BASH_SOURCE[0]%/*}/.." && pwd)"
echo "Build env: TEMP=$TEMP | mingw64 on PATH | ccache sloppy | CCACHE_BASEDIR=$CCACHE_BASEDIR | USERPROFILE=$USERPROFILE"

# Auto-generate Ed25519 keypair if not present (idempotent — exits immediately if key exists)
"${BASH_SOURCE[0]%/*}/ensure-keypair.sh" || true
