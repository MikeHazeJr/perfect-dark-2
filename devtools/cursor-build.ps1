#Requires -Version 5.1
<#
.SYNOPSIS
    Build client and/or server into the "Cursor Build" directory at the repo root.

.DESCRIPTION
    Wrapper around build-headless.ps1 with -OutputDir "Cursor Build".
    Use -CommitPush to commit + push before building; -UseNextVersion to bump
    CMakeLists.txt to max(CMake, v* tags) + 1 (see version-util.ps1).

.PARAMETER Target
    client | server | all (default: all)

.PARAMETER Clean
    Remove the Cursor Build directory before configuring.

.PARAMETER Verbose
    Full compiler output.

.PARAMETER Version
    Optional X.Y.Z override for this build (overrides -UseNextVersion).

.PARAMETER UseNextVersion
    Set CMakeLists to next semver (max of CMake + git tags, then patch + 1).

.PARAMETER CommitPush
    Alias for -AutoCommit.

.EXAMPLE
    .\devtools\cursor-build.ps1
    .\devtools\cursor-build.ps1 -CommitPush
    .\devtools\cursor-build.ps1 -UseNextVersion -CommitPush
#>

param(
    [ValidateSet("client", "server", "all")]
    [string]$Target = "all",

    [string]$Version = "",

    [switch]$Clean,

    [switch]$Verbose,

    [Alias("CommitPush")]
    [switch]$AutoCommit,

    [switch]$UseNextVersion
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$Headless  = Join-Path $ScriptDir "build-headless.ps1"

if (-not (Test-Path $Headless)) {
    Write-Error "Missing build-headless.ps1 next to this script: $Headless"
    exit 1
}

if ($Version -ne "") {
    & $Headless -Target $Target -OutputDir "Cursor Build" `
        -Clean:$Clean -Verbose:$Verbose -AutoCommit:$AutoCommit `
        -UseNextVersion:$UseNextVersion -Version $Version
} else {
    & $Headless -Target $Target -OutputDir "Cursor Build" `
        -Clean:$Clean -Verbose:$Verbose -AutoCommit:$AutoCommit `
        -UseNextVersion:$UseNextVersion
}
exit $LASTEXITCODE
