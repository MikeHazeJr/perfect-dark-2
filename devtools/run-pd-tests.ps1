#Requires -Version 5.1
<#
.SYNOPSIS
    Build and run pd-tests from an isolated per-session build directory.

.DESCRIPTION
    Concurrent AI/code sessions should not share Build/. This helper builds
    only the pd-tests CMake target through devtools/build-session.ps1, then
    runs pd-tests.exe with an optional Catch2 selector such as "[manifest]" or
    "[catalog][provider][static]".

.EXAMPLE
    .\devtools\run-pd-tests.ps1 -Session qnet1 -Selector "[netbuf]"

.EXAMPLE
    .\devtools\run-pd-tests.ps1 -Session qcat1 -Selector "[catalog][provider][static]"

.EXAMPLE
    .\devtools\run-pd-tests.ps1 -Session qcat1 -Scope catalog-provider

.EXAMPLE
    .\devtools\run-pd-tests.ps1 -ListScopes

.EXAMPLE
    .\devtools\run-pd-tests.ps1 -Session qall1

.EXAMPLE
    .\devtools\run-pd-tests.ps1 -Session qlist1 -ListTags -NoBuild
#>

param(
    [Parameter(Mandatory = $false)]
    [string]$Session,

    [string]$Selector = "",

    [ValidateSet(
        "catalog",
        "catalog-provider",
        "catalog-identity",
        "input",
        "manifest",
        "save",
        "netbuf",
        "connectcode",
        "network-lifecycle",
        "spawn"
    )]
    [string]$Scope = "",

    [switch]$ListTags,
    [switch]$ListTests,
    [switch]$ListScopes,
    [switch]$NoBuild,
    [switch]$Clean,
    [switch]$BuildVerbose,

    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$CatchArgs = @()
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectDir = Split-Path -Parent $ScriptDir
$SessionBuildRoot = Join-Path $ProjectDir ".claude\session-builds"
$BuildSession = Join-Path $ScriptDir "build-session.ps1"
$ScopeSelectors = [ordered]@{
    "catalog" = "[catalog]"
    "catalog-provider" = "[catalog][provider][static]"
    "catalog-identity" = "[catalog][identity][static]"
    "input" = "[input]"
    "manifest" = "[manifest]"
    "save" = "[save][migration]"
    "netbuf" = "[netbuf]"
    "connectcode" = "[connectcode]"
    "network-lifecycle" = "[lifecycle]"
    "spawn" = "[spawn-weapon]"
}

function ConvertTo-SafeSessionName([string]$value) {
    $name = $value.Trim()
    if ($name -eq "") {
        throw "Session name is empty."
    }

    $name = $name -replace '[^A-Za-z0-9._-]+', '-'
    $name = $name.Trim([char[]]".-_")
    if ($name.Length -gt 64) {
        $name = $name.Substring(0, 64).TrimEnd([char[]]".-_")
    }
    if ($name -eq "" -or $name -eq "." -or $name -eq "..") {
        throw "Session name '$value' does not contain any safe path characters."
    }

    $reserved = @("CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9", "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9")
    if ($reserved -contains $name.ToUpperInvariant()) {
        $name = "session-$name"
    }

    return $name
}

if ($ListScopes) {
    $ScopeSelectors.GetEnumerator() |
        ForEach-Object {
            [PSCustomObject]@{
                Scope = $_.Key
                Selector = $_.Value
            }
        } |
        Format-Table -AutoSize
    exit 0
}

if ($Session -eq "") {
    throw "Specify -Session <id>, or use -ListScopes."
}

$listModes = @($ListTags.IsPresent, $ListTests.IsPresent) | Where-Object { $_ }
if ($listModes.Count -gt 1) {
    throw "Choose only one of -ListTags or -ListTests."
}

if ($Scope -ne "" -and $Selector -ne "") {
    throw "Use either -Scope or -Selector, not both."
}

if (-not (Test-Path -LiteralPath $BuildSession)) {
    throw "Missing isolated build wrapper: $BuildSession"
}

$sessionName = ConvertTo-SafeSessionName $Session
$buildDir = Join-Path $SessionBuildRoot $sessionName
$testsExe = Join-Path $buildDir "pd-tests.exe"
$effectiveSelector = if ($Scope -ne "") { $ScopeSelectors[$Scope] } else { $Selector }

if (-not $NoBuild) {
    $buildArgs = @("-Session", $sessionName, "-Target", "tests")
    if ($Clean) { $buildArgs += "-Clean" }
    if ($BuildVerbose) { $buildArgs += "-Verbose" }

    $childArgs = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $BuildSession) + $buildArgs
    & powershell.exe @childArgs
    $buildExit = $LASTEXITCODE
    if ($buildExit -ne 0) {
        exit $buildExit
    }
}

if (-not (Test-Path -LiteralPath $testsExe)) {
    throw "pd-tests.exe not found at $testsExe. Run without -NoBuild first."
}

$mingwBin = "C:\msys64\mingw64\bin"
if (Test-Path -LiteralPath $mingwBin) {
    $env:PATH = "$mingwBin;$env:PATH"
}

$testArgs = @()
if ($ListTags) {
    $testArgs += "--list-tags"
} elseif ($ListTests) {
    $testArgs += "--list-tests"
} elseif ($effectiveSelector -ne "") {
    $testArgs += $effectiveSelector
}
if ($CatchArgs.Count -gt 0) {
    $testArgs += $CatchArgs
}

Write-Host ""
Write-Host "  Perfect Dark PC Port - Targeted Tests" -ForegroundColor Cyan
Write-Host "  Session:  $sessionName" -ForegroundColor Gray
Write-Host "  BuildDir: $buildDir" -ForegroundColor DarkGray
Write-Host "  Selector: $(if ($effectiveSelector -ne '') { $effectiveSelector } elseif ($ListTags) { '--list-tags' } elseif ($ListTests) { '--list-tests' } else { '<all>' })" -ForegroundColor Gray
Write-Host ""

Push-Location $ProjectDir
try {
    & $testsExe @testArgs
    $testExit = $LASTEXITCODE
} finally {
    Pop-Location
}

Write-Host ""
Write-Host "  Session build cleanup:" -ForegroundColor Cyan
Write-Host "    .\devtools\build-session.ps1 -Remove -Session $sessionName" -ForegroundColor Gray

exit $testExit
