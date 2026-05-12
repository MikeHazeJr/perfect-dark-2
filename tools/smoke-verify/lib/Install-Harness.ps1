#Requires -Version 5.1
<#
.SYNOPSIS
    Clean-install harness for the smoke verify gate.

.DESCRIPTION
    Builds a per-test install directory from a known source binary and
    a known ROM, then exposes the run path. Three install states:

      clean       fresh dir, no data/<romid>/, no pd.ini
      prefilled   fresh dir with data/<romid>/ from .claude/smoke-verify-cache
      current     points at an existing install (only via -Install on
                  the runner; never the default)

    The runner module dot-sources this script and calls New-SmokeInstall.

    The source binary defaults to Build/PerfectDark.exe, falling back
    through the user's session-build directories in
    .claude/session-builds/*/PerfectDark.exe if the canonical Build/ is
    missing. The ROM defaults to the file matching pd.*.z64 in the
    project root or the canonical Build/ directory.
#>

Set-StrictMode -Version Latest

function Get-SmokeProjectRoot {
    [CmdletBinding()] param()
    $scriptPath = $PSCommandPath
    if (-not $scriptPath) { $scriptPath = $MyInvocation.MyCommand.Path }
    $libDir = Split-Path -Parent $scriptPath
    $smokeDir = Split-Path -Parent $libDir
    $toolsDir = Split-Path -Parent $smokeDir
    $projectRoot = Split-Path -Parent $toolsDir
    return $projectRoot
}

function Find-SourceBinary {
    [CmdletBinding()] param(
        [Parameter(Mandatory)] [string] $ProjectRoot,
        [string] $ExplicitPath = ""
    )

    if ($ExplicitPath -and (Test-Path -LiteralPath $ExplicitPath)) {
        return (Resolve-Path -LiteralPath $ExplicitPath).Path
    }

    $candidates = @()
    $candidates += Join-Path $ProjectRoot "Build\PerfectDark.exe"
    $sessionRoot = Join-Path $ProjectRoot ".claude\session-builds"
    if (Test-Path -LiteralPath $sessionRoot) {
        $candidates += Get-ChildItem -LiteralPath $sessionRoot -Directory -ErrorAction SilentlyContinue |
            ForEach-Object { Join-Path $_.FullName "PerfectDark.exe" }
    }

    foreach ($c in $candidates) {
        if (Test-Path -LiteralPath $c) {
            return (Resolve-Path -LiteralPath $c).Path
        }
    }
    return $null
}

function Find-SourceRom {
    [CmdletBinding()] param(
        [Parameter(Mandatory)] [string] $ProjectRoot,
        [string] $ExplicitPath = ""
    )

    if ($ExplicitPath -and (Test-Path -LiteralPath $ExplicitPath)) {
        return (Resolve-Path -LiteralPath $ExplicitPath).Path
    }

    $roots = @(
        (Join-Path $ProjectRoot "Build"),
        $ProjectRoot
    )

    foreach ($r in $roots) {
        if (-not (Test-Path -LiteralPath $r)) { continue }
        $roms = Get-ChildItem -LiteralPath $r -Filter "pd.*.z64" -File -ErrorAction SilentlyContinue
        foreach ($rom in $roms) {
            return $rom.FullName
        }
    }
    return $null
}

function Get-RomIdFromName {
    [CmdletBinding()] param([Parameter(Mandatory)] [string] $RomPath)
    $name = [System.IO.Path]::GetFileNameWithoutExtension($RomPath)
    # pd.<romid>.z64 -> name is "pd.<romid>"; strip leading "pd."
    if ($name -match '^pd\.(.+)$') { return $matches[1] }
    return $name
}

function New-SmokeInstall {
    [CmdletBinding()] param(
        [Parameter(Mandatory)] [string] $RunRoot,
        [Parameter(Mandatory)] [string] $TestName,
        [string] $InstallState = "clean",
        [string] $SourceBinary = "",
        [string] $SourceRom = "",
        [string] $ProjectRoot = ""
    )

    if (-not $ProjectRoot) { $ProjectRoot = Get-SmokeProjectRoot }

    $stampedName = "{0}-{1}" -f ((Get-Date).ToUniversalTime().ToString("yyyyMMddTHHmmssZ")), $TestName
    $installDir = Join-Path $RunRoot $stampedName
    New-Item -ItemType Directory -Path $installDir -Force | Out-Null

    $bin = Find-SourceBinary -ProjectRoot $ProjectRoot -ExplicitPath $SourceBinary
    if (-not $bin) {
        throw "Cannot find PerfectDark.exe to seed the smoke install. Build the client first or pass -SourceBinary."
    }

    $rom = Find-SourceRom -ProjectRoot $ProjectRoot -ExplicitPath $SourceRom
    if (-not $rom) {
        throw "Cannot find pd.<romid>.z64 to seed the smoke install. Place a ROM in the project root or pass -SourceRom."
    }

    Copy-Item -LiteralPath $bin -Destination (Join-Path $installDir "PerfectDark.exe") -Force
    Copy-Item -LiteralPath $rom -Destination (Join-Path $installDir ([System.IO.Path]::GetFileName($rom))) -Force

    $romId = Get-RomIdFromName -RomPath $rom

    if ($InstallState -eq "prefilled") {
        $cache = Join-Path $ProjectRoot (".claude\smoke-verify-cache\$romId")
        if (Test-Path -LiteralPath $cache) {
            $dataDir = Join-Path $installDir "data"
            New-Item -ItemType Directory -Path $dataDir -Force | Out-Null
            Copy-Item -LiteralPath $cache -Destination (Join-Path $dataDir $romId) -Recurse -Force
        } else {
            Write-Warning ("Prefilled cache not found at {0}; falling back to clean state for this run." -f $cache)
        }
    }

    return [PSCustomObject]@{
        InstallDir = $installDir
        SourceBinary = $bin
        SourceRom = $rom
        RomId = $romId
        InstallState = $InstallState
    }
}

function Get-SmokeLogPath {
    [CmdletBinding()] param([Parameter(Mandatory)] [string] $InstallDir)
    return (Join-Path $InstallDir "pd-client.log")
}
