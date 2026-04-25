#!/usr/bin/env bash
# ensure-keypair.sh -- Auto-generate Ed25519 keypair if dev-keys/ is missing one.
# Called automatically from build-env.sh before every bash build. Idempotent.
# Safe to run repeatedly -- exits immediately if the key already exists.

SCRIPT_DIR="$(cd "${BASH_SOURCE[0]%/*}" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
PRIV="$PROJECT_DIR/dev-keys/ed25519-private.pem"

[ -f "$PRIV" ] && exit 0

echo "  [keygen] dev-keys/ed25519-private.pem not found -- generating Ed25519 keypair..."

KEYGEN_PS1="$SCRIPT_DIR/keygen.ps1"
# cygpath converts MSYS2 path to Windows path for PowerShell
KEYGEN_WIN="$(cygpath -w "$KEYGEN_PS1" 2>/dev/null || echo "$KEYGEN_PS1")"

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$KEYGEN_WIN" || {
    echo "  [keygen] WARNING: Keypair generation failed. Builds succeed but releases won't be signable."
}
