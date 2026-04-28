#Requires -Version 5.1
<#
.SYNOPSIS
    Run a build in an isolated per-session build directory.

.DESCRIPTION
    Concurrent AI/code sessions must not share the root Build/ directory. This
    wrapper keeps the canonical build logic in build-headless.ps1, but gives
    each session its own CMake/Ninja directory under .claude/session-builds/.

.EXAMPLE
    .\devtools\build-session.ps1 -Session s500 -Target all
    .\devtools\build-session.ps1 -Session s500 -Target client -Clean
    .\devtools\build-session.ps1 -List
    .\devtools\build-session.ps1 -Remove -Session s500
    .\devtools\build-session.ps1 -RemoveAll
#>

param(
    [ValidateSet("client", "server", "all")]
    [string]$Target = "all",

    # Stable per-session identifier. Reuse it for incremental rebuilds inside
    # one session; choose a different value for simultaneous sessions.
    [string]$Session = "",

    [string]$Version = "",

    [switch]$Clean,
    [switch]$Verbose,

    # Maintenance modes.
    [switch]$List,
    [switch]$Remove,
    [switch]$RemoveAll,

    # Only affects stale-lock cleanup for -Remove / -RemoveAll.
    [switch]$Force
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectDir = Split-Path -Parent $ScriptDir
$SessionBuildRoot = Join-Path $ProjectDir ".claude\session-builds"
$LockDir = Join-Path $SessionBuildRoot ".locks"

function Write-Info([string]$text) { Write-Host $text -ForegroundColor Gray }
function Write-Warn([string]$text) { Write-Host $text -ForegroundColor Yellow }
function Write-Ok([string]$text)   { Write-Host $text -ForegroundColor Green }

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

function Get-SessionName([bool]$requireStable) {
    if ($Session -ne "") {
        return ConvertTo-SafeSessionName $Session
    }
    if ($env:PD_BUILD_SESSION) {
        return ConvertTo-SafeSessionName $env:PD_BUILD_SESSION
    }
    if ($env:CODEX_SESSION_ID) {
        return ConvertTo-SafeSessionName $env:CODEX_SESSION_ID
    }
    if ($env:CLAUDE_SESSION_ID) {
        return ConvertTo-SafeSessionName $env:CLAUDE_SESSION_ID
    }
    if ($requireStable) {
        throw "Specify -Session <id> or set PD_BUILD_SESSION before removing a session build."
    }
    return ConvertTo-SafeSessionName "manual-$PID"
}

function Get-SafeChildPath([string]$childName) {
    $rootFull = [System.IO.Path]::GetFullPath($SessionBuildRoot).TrimEnd('\')
    $target = Join-Path $SessionBuildRoot $childName
    $targetFull = [System.IO.Path]::GetFullPath($target).TrimEnd('\')
    if (-not $targetFull.StartsWith($rootFull + "\", [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing path outside session build root: $targetFull"
    }
    return $targetFull
}

function Get-LockPath([string]$name) {
    return Join-Path $LockDir "$name.lock"
}

function Test-SessionLocked([string]$name) {
    return Test-Path -LiteralPath (Get-LockPath $name)
}

function Enter-SessionBuildLock([string]$name) {
    if (-not (Test-Path -LiteralPath $LockDir)) {
        New-Item -ItemType Directory -Path $LockDir -Force | Out-Null
    }

    $lockPath = Get-LockPath $name
    try {
        $stream = [System.IO.File]::Open($lockPath, [System.IO.FileMode]::CreateNew, [System.IO.FileAccess]::Write, [System.IO.FileShare]::None)
        $bytes = [System.Text.Encoding]::UTF8.GetBytes("pid=$PID`nstarted=$([DateTime]::Now.ToString('s'))`nproject=$ProjectDir`n")
        $stream.Write($bytes, 0, $bytes.Length)
        $stream.Flush()
        return @{ Path = $lockPath; Stream = $stream }
    } catch [System.IO.IOException] {
        throw "Session build '$name' is already locked. Use a unique -Session for parallel builds, or remove stale lock '$lockPath' after confirming no build is running."
    }
}

function Exit-SessionBuildLock($lock) {
    if ($null -eq $lock) { return }
    try { $lock.Stream.Dispose() } catch {}
    try { Remove-Item -LiteralPath $lock.Path -Force -ErrorAction SilentlyContinue } catch {}
}

function Show-SessionBuilds {
    if (-not (Test-Path -LiteralPath $SessionBuildRoot)) {
        Write-Info "No session build directory exists yet: $SessionBuildRoot"
        return
    }

    $rows = @()
    Get-ChildItem -LiteralPath $SessionBuildRoot -Directory -Force |
        Where-Object { $_.Name -ne ".locks" } |
        Sort-Object LastWriteTime -Descending |
        ForEach-Object {
            $rows += [PSCustomObject]@{
                Session = $_.Name
                Locked = if (Test-SessionLocked $_.Name) { "yes" } else { "no" }
                LastWrite = $_.LastWriteTime
                Path = $_.FullName
            }
        }

    if ($rows.Count -eq 0) {
        Write-Info "No session builds found under $SessionBuildRoot"
    } else {
        $rows | Format-Table -AutoSize
    }
}

function Remove-SessionBuild([string]$name) {
    $targetFull = Get-SafeChildPath $name
    $lockPath = Get-LockPath $name

    if ((Test-Path -LiteralPath $lockPath) -and -not $Force) {
        Write-Warn "Skipping locked session '$name'. If no build is running, rerun with -Force."
        return
    }

    if (Test-Path -LiteralPath $targetFull) {
        Remove-Item -LiteralPath $targetFull -Recurse -Force
        Write-Ok "Removed session build: $targetFull"
    } else {
        Write-Info "Session build not found: $targetFull"
    }

    if ($Force -and (Test-Path -LiteralPath $lockPath)) {
        Remove-Item -LiteralPath $lockPath -Force
        Write-Ok "Removed stale lock: $lockPath"
    }
}

if ($List) {
    Show-SessionBuilds
    exit 0
}

if ($RemoveAll) {
    if (-not (Test-Path -LiteralPath $SessionBuildRoot)) {
        Write-Info "No session build directory exists yet: $SessionBuildRoot"
        exit 0
    }

    Get-ChildItem -LiteralPath $SessionBuildRoot -Directory -Force |
        Where-Object { $_.Name -ne ".locks" } |
        ForEach-Object { Remove-SessionBuild $_.Name }
    exit 0
}

if ($Remove) {
    $nameToRemove = Get-SessionName $true
    Remove-SessionBuild $nameToRemove
    exit 0
}

$sessionName = Get-SessionName $false
$buildDir = Get-SafeChildPath $sessionName
$headless = Join-Path $ScriptDir "build-headless.ps1"
$gitForWindows = "C:\Program Files\Git\cmd"

if (-not (Test-Path -LiteralPath $headless)) {
    throw "Missing canonical build script: $headless"
}

Write-Host ""
Write-Host "  Perfect Dark PC Port - Session Build" -ForegroundColor Cyan
Write-Host "  Session:  $sessionName" -ForegroundColor Gray
Write-Host "  Target:   $Target" -ForegroundColor Gray
Write-Host "  BuildDir: $buildDir" -ForegroundColor DarkGray
Write-Host ""

$lock = $null
try {
    $lock = Enter-SessionBuildLock $sessionName

    $buildArgs = @(
        "-Target", $Target,
        "-OutputDir", $buildDir
    )
    if ($Version -ne "") { $buildArgs += @("-Version", $Version) }
    if ($Clean) { $buildArgs += "-Clean" }
    if ($Verbose) { $buildArgs += "-Verbose" }

    # build-headless.ps1 intentionally calls exit; run it in a child process so
    # this wrapper can always release the per-session lock afterward.
    if (Test-Path -LiteralPath (Join-Path $gitForWindows "git.exe")) {
        $env:PATH = "$gitForWindows;$env:PATH"
    }
    $childArgs = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $headless) + $buildArgs
    & powershell.exe @childArgs
    $exitCode = $LASTEXITCODE
} finally {
    Exit-SessionBuildLock $lock
}

Write-Host ""
Write-Host "  Session build cleanup:" -ForegroundColor Cyan
Write-Host "    .\devtools\build-session.ps1 -Remove -Session $sessionName" -ForegroundColor Gray

exit $exitCode
