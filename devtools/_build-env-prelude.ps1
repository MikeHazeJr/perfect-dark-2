# _build-env-prelude.ps1 -- Dot-source at the top of every build/dev script.
# Ensures TEMP/TMP point to a writable directory and MinGW64 is on PATH.
# Idempotent: safe to source multiple times; does not double-add PATH entries.
#
# Usage (from any build script in devtools/ or devtools/dev-window-v2/):
#   . (Join-Path $ScriptDir "_build-env-prelude.ps1")         # from devtools/
#   . (Join-Path $PSScriptRoot ".." "_build-env-prelude.ps1") # from devtools/subdirs

# TEMP/TMP -- hardcoded to a known-writable path. Create the directory if absent.
# Some sandbox / code-session environments inherit C:\Windows as TEMP, which is
# read-only and causes cc1.exe to crash when writing intermediate files.
$env:TEMP = "C:\Users\mikeh\AppData\Local\Temp"
$env:TMP  = $env:TEMP
if (-not (Test-Path $env:TEMP)) {
    New-Item -ItemType Directory -Path $env:TEMP -Force | Out-Null
}

# PATH -- prepend MinGW64 + MSYS2 usr/bin only if not already present.
# The check prevents double-adding on re-runs or nested dot-source calls.
if ($env:PATH -notlike "*C:\msys64\mingw64\bin*") {
    $env:PATH = "C:\msys64\mingw64\bin;C:\msys64\usr\bin;$env:PATH"
}

$env:MSYSTEM           = "MINGW64"
$env:MINGW_PREFIX      = "/mingw64"
$env:CCACHE_SLOPPINESS = "pch_defines,time_macros"

Write-Host "Build env: TEMP=$($env:TEMP) | mingw64 on PATH | ccache sloppy" -ForegroundColor DarkGray
