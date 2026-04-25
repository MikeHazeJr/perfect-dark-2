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

# PATH -- force MinGW64 + MSYS2 usr/bin to the front (order matters).
# Some environments already contain these entries later in PATH, so a plain
# "if contains" check can leave another toolchain (for example devkitPro
# MSYS/Cygwin cmake) ahead of the intended one.
$mingwBin = "C:\msys64\mingw64\bin"
$msysUsr  = "C:\msys64\usr\bin"
$pathParts = @()
if ($env:PATH) {
    $pathParts = $env:PATH -split ';'
}
$filtered = @()
foreach ($p in $pathParts) {
    if (-not $p) { continue }
    if ($p -ieq $mingwBin) { continue }
    if ($p -ieq $msysUsr) { continue }
    $filtered += $p
}
$env:PATH = "$mingwBin;$msysUsr"
if ($filtered.Count -gt 0) {
    $env:PATH = "$env:PATH;" + ($filtered -join ';')
}

$env:MSYSTEM           = "MINGW64"
$env:MINGW_PREFIX      = "/mingw64"
$env:CCACHE_SLOPPINESS = "pch_defines,time_macros,include_file_mtime,include_file_ctime"
$env:CCACHE_BASEDIR    = Split-Path (Split-Path $MyInvocation.MyCommand.Path -Parent) -Parent

# Dev Window / GUI tools set PD_BUILD_ENV_QUIET=1 before dot-sourcing to avoid
# printing into a visible console during startup.
if ($env:PD_BUILD_ENV_QUIET -ne '1') {
    Write-Host "Build env: TEMP=$($env:TEMP) | mingw64 on PATH | ccache sloppy" -ForegroundColor DarkGray
}
