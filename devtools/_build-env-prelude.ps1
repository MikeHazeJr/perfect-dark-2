# _build-env-prelude.ps1 -- Dot-source at the top of every build/dev script.
# Ensures TEMP/TMP point to a writable directory and MinGW64 is on PATH.
# Idempotent: safe to source multiple times; does not double-add PATH entries.
#
# Usage (from any build script in devtools/ or devtools/dev-window-v2/):
#   . (Join-Path $ScriptDir "_build-env-prelude.ps1")         # from devtools/
#   . (Join-Path $PSScriptRoot ".." "_build-env-prelude.ps1") # from devtools/subdirs

# TEMP/TMP -- current user's Local\Temp (never hardcode another profile path).
# Some environments inherit C:\Windows as TEMP, which breaks cc1.exe.
$goodTemp = $null
if ($env:LOCALAPPDATA) {
    $goodTemp = Join-Path $env:LOCALAPPDATA "Temp"
}
if (-not $goodTemp -and $env:USERPROFILE) {
    $goodTemp = Join-Path $env:USERPROFILE "AppData\Local\Temp"
}
if (-not $goodTemp) {
    $goodTemp = [System.IO.Path]::GetTempPath().TrimEnd('\')
}
$env:TEMP = $goodTemp
$env:TMP  = $goodTemp
if (-not (Test-Path -LiteralPath $env:TEMP)) {
    try {
        New-Item -ItemType Directory -LiteralPath $env:TEMP -Force -ErrorAction Stop | Out-Null
    } catch {
        $fallback = [System.IO.Path]::GetTempPath().TrimEnd('\')
        $env:TEMP = $fallback
        $env:TMP  = $fallback
    }
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
