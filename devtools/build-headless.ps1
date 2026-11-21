#Requires -Version 5.1
<#
.SYNOPSIS
    Headless build script for Perfect Dark PC Port  -  no GUI, pure console output.

.DESCRIPTION
    Runs the same CMake configure + build pipeline as dev-window-v2.ps1 but without
    any WinForms or windows. Suitable for CI, code sessions, and terminal use.
    Uses Ninja generator, unified Build/ directory, ccache, and mold linker.

.PARAMETER Target
    What to build: client, updater, tests, or all (default: all).
    The standalone pd-server target was removed; listen-host is the shipping server path.

.PARAMETER Clean
    Remove the build directory before configuring (clean build).

.PARAMETER AutoCommit
    Commit and push pending changes before building. Default: OFF.
    release.ps1 always commits; this flag is opt-in for development builds.

.PARAMETER Verbose
    Show full compiler output. Without this flag, only errors and summary lines
    are printed during the compile step.

.PARAMETER OutputDir
    CMake build directory relative to the project root, or an absolute path.
    Default is "Build". Use "Cursor Build" for the Cursor-focused out-of-tree folder.

.PARAMETER UseNextVersion
    Before configuring, set CMakeLists.txt to max(CMake version, all vX.Y.Z git tags) + 1 patch.
    Implies a version bump for this build; combine with -AutoCommit to commit the file change.

.PARAMETER CommitPush
    Alias for -AutoCommit: commit pending changes (with SP-9 guard) and push current branch.

.EXAMPLE
    .\build-headless.ps1
    .\build-headless.ps1 -Target client -Clean
    .\build-headless.ps1 -Target tests
    .\build-headless.ps1 -AutoCommit
    powershell -File build-headless.ps1 -Target all -Clean
    .\build-headless.ps1 -OutputDir "Cursor Build"
    .\build-headless.ps1 -UseNextVersion -CommitPush
#>

param(
    [ValidateSet("client", "updater", "tests", "settings-tests", "probe", "all")]
    [string]$Target = "all",

    # Version override in "X.Y.Z" format. If omitted, reads VERSION_SEM_* from CMakeLists.txt
    # (same as what the Dev Window does with Get-ProjectVersion).
    [string]$Version = "",

    [switch]$Clean,

    [Alias("CommitPush")]
    [switch]$AutoCommit,

    [switch]$Verbose,

    # Limit Ninja compiler concurrency on memory-constrained hosts. Zero keeps
    # Ninja's default parallelism for ordinary builds.
    [ValidateRange(0, 64)]
    [int]$Jobs = 0,

    [string]$OutputDir = "",

    [switch]$UseNextVersion,

    [switch]$SelfTest,

    [switch]$RetainHousekeeping,
    [string]$BuildRetentionContextFile = "",

    # Dev-mod selection copied into <install>/mods. "" = manifest "dev":true
    # mods (default); "all" = every listed mod; "none" = no dev mods;
    # "id1,id2" = only the named mods. See dev-mods/README.md.
    [string]$DevMods = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

# Retained child protocol: no native launch or housekeeping before validation.
function Assert-HeadlessRetainedOptions {
    if (-not $RetainHousekeeping) {
        if ($BuildRetentionContextFile) { throw 'Retained context requires -RetainHousekeeping.' }
        return
    }
    if ($Clean -or $AutoCommit -or $UseNextVersion -or $SelfTest -or $DevMods) {
        throw 'Retained child refuses cleanup, Git/version, self-test and mod-copy modes before setup.'
    }
    if ($Target -notin @('tests','settings-tests','probe')) {
        throw 'Retained client/updater/all install-copy preservation is not integrated.'
    }
    if (-not $OutputDir -or -not $BuildRetentionContextFile) {
        throw 'Retained child requires explicit isolated output and invocation context.'
    }
}

function Assert-HeadlessRetainedLaunchers([string]$Directory, [bool]$RequireExplicit = $false) {
    $cache = Join-Path $Directory 'CMakeCache.txt'
    if (-not (Test-Path -LiteralPath $cache)) {
        if ($RequireExplicit) { throw 'Retained configure produced no launcher proof.' }
        return
    }
    Assert-SmokePlainTree -Path $cache
    $seen = @{}
    foreach ($line in [IO.File]::ReadAllLines($cache)) {
        if ($line -match '^CMAKE_(C|CXX)_COMPILER_LAUNCHER(?::[^=]+)?=(.*)$') {
            if ($seen.ContainsKey($Matches[1])) { throw 'Duplicate cached compiler launcher.' }
            $seen[$Matches[1]] = $true
            if ($Matches[2] -cne '') { throw 'Retained child refuses an effective cached compiler launcher.' }
        }
    }
    if ($RequireExplicit -and (-not $seen.ContainsKey('C') -or -not $seen.ContainsKey('CXX'))) {
        throw 'Retained configure lacks explicit empty C and CXX launchers.'
    }
}

function Initialize-HeadlessRetainedContext {
    if (-not $RetainHousekeeping) { return }
    Assert-SmokePlainTree -Path $BuildRetentionContextFile
    $context = [IO.File]::ReadAllText($BuildRetentionContextFile) | ConvertFrom-Json
    foreach ($field in @('schema','run_id','project_root','target','session','build_dir','reviewed_child_sha256')) {
        if ($context.$field -isnot [string]) { throw ('Retained context must have scalar string '+$field) }
    }
    if ($context.session -cnotmatch '^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$' -or
        $context.reviewed_child_sha256 -cnotmatch '^[0-9a-f]{64}$' -or
        ($context.owner.pid -isnot [int] -and $context.owner.pid -isnot [long]) -or
        $context.owner.pid -le 0 -or $context.owner.pid -gt [int]::MaxValue -or
        ($context.owner.creation_ticks -isnot [long] -and $context.owner.creation_ticks -isnot [int]) -or
        $context.owner.creation_ticks -le 0) {
        throw 'Retained context owner/session/hash types are invalid.'
    }
    $receiptRoot = Join-Path $ProjectDir '.claude/smoke-storage/receipts'
    if ($context.schema -cne 'pd2.build-retention-context.v1' -or
        $context.run_id -cnotmatch '^runner-[0-9a-f]{32}$' -or
        $context.project_root -cne $ProjectDir -or $context.target -cne $Target) {
        throw 'Retained child context identity mismatch.'
    }
    $runRoot = Join-Path $receiptRoot $context.run_id
    if (-not [string]::Equals([IO.Path]::GetFullPath($BuildRetentionContextFile),
            (Join-Path $runRoot 'build-context.json'), [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Retained context escaped the exact receipt root.'
    }
    Assert-SmokePlainTree -Path $runRoot -Descendants
    $marker = [IO.File]::ReadAllText((Join-Path $runRoot 'run-owner.json')) | ConvertFrom-Json
    if ($marker.schema -isnot [string] -or $marker.run_id -isnot [string] -or
        $marker.retain_housekeeping -isnot [bool] -or
        ($marker.owner.pid -isnot [int] -and $marker.owner.pid -isnot [long]) -or
        ($marker.owner.creation_ticks -isnot [long] -and $marker.owner.creation_ticks -isnot [int])) {
        throw 'Retained owner marker types are invalid.'
    }
    if ($marker.schema -cne 'pd2.smoke-runner-retention.v1' -or
        $marker.run_id -cne $context.run_id -or $marker.retain_housekeeping -ne $true -or
        $marker.owner.pid -ne $context.owner.pid -or
        $marker.owner.creation_ticks -ne $context.owner.creation_ticks) {
        throw 'Retained context owner marker mismatch.'
    }
    $current = Get-CimInstance Win32_Process -Filter ('ProcessId=' + $PID) -ErrorAction Stop
    if ($current.ParentProcessId -ne $context.owner.pid) { throw 'Retained child parent mismatch.' }
    $parent = [Diagnostics.Process]::GetProcessById([int]$context.owner.pid)
    try {
        if ($parent.HasExited -or $parent.StartTime.ToUniversalTime().ToFileTimeUtc() -ne $context.owner.creation_ticks) {
            throw 'Retained child parent creation identity mismatch.'
        }
    } finally { $parent.Dispose() }
    $actualSha = (Get-FileHash -Algorithm SHA256 -LiteralPath $PSCommandPath).Hash.ToLowerInvariant()
    if (-not $context.reviewed_child_sha256 -or $context.reviewed_child_sha256 -cne $actualSha) {
        throw 'Retained child lacks the exact reviewed source capability.'
    }
    $sessionRoot = [IO.Path]::GetFullPath((Join-Path $ProjectDir '.claude/session-builds')).TrimEnd('\')
    $output = [IO.Path]::GetFullPath($BuildDir)
    if (-not $output.StartsWith($sessionRoot + '\', [StringComparison]::OrdinalIgnoreCase) -or
        -not [string]::Equals($output, [IO.Path]::GetFullPath($context.build_dir), [StringComparison]::OrdinalIgnoreCase) -or
        -not [string]::Equals($output, (Join-Path $sessionRoot $context.session), [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Retained child output escaped the invocation session.'
    }
    Assert-SmokePlainTree -Path $output -Descendants
    if (-not (Test-Path -LiteralPath (Join-Path $ProjectDir 'dev-keys/ed25519-private.pem'))) {
        throw 'Retained child refuses key generation before native launch.'
    }
    Assert-HeadlessRetainedLaunchers $output
    [void](Assert-SmokeStoragePreflight -ProjectRoot $ProjectDir -Build)
    # Each actor uses the existing accounted receipt publisher with its real PID.
    # Parent context remains read-only; do not impersonate its storage ownership.
    $script:HeadlessRetainedContext = New-SmokeRunnerRetentionContext -ProjectRoot $ProjectDir
    [void](Write-SmokeRunnerRetainedJson -Context $script:HeadlessRetainedContext -Name 'child-binding.json' -Value @{
        parent_run_id=$context.run_id; parent_owner=$context.owner; child_owner=$script:HeadlessRetainedContext.Owner;
        source_sha256=$actualSha; build_dir=$output; target=$Target
    })
    $script:HeadlessRetainedRunRoot = $script:HeadlessRetainedContext.RunRoot
    $script:HeadlessRetainedSourceSha256 = $actualSha
}

function Write-HeadlessRetainedBytes([string]$Path, [byte[]]$Bytes) {
    if ($Bytes.Length) { throw 'Known receipt payloads require the existing locked storage publisher.' }
    Assert-SmokePlainTree -Path $Path
    $stream = [IO.File]::Open($Path, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
    try { $stream.Write($Bytes, 0, $Bytes.Length); $stream.Flush($true) }
    finally { $stream.Dispose() }
}

function Write-HeadlessRetainedReceipt([string]$Name, [byte[]]$Bytes, [hashtable]$Binding = @{}) {
    return Write-SmokeRunnerRetainedJson -Context $script:HeadlessRetainedContext -Name $Name -Value @{
        schema='pd2.headless-retained-bytes.v1'; bytes=$Bytes.Length; raw_base64=[Convert]::ToBase64String($Bytes); step=$Binding
    }
}

Assert-HeadlessRetainedOptions

# Worktree-redirect guard (c070): pure resolver, unit-tested via -SelfTest.
# Builds are NEVER allowed to operate on a .claude\worktrees\<name> copy -- if the
# script is invoked from inside one, strip back to the real repo root so builds
# always hit the main working files. Returns $Path unchanged when it is not a
# worktree path. (The deprecated dev-window-v2.ps1 / _dev-window.ps1 keep their own
# inline copies; fold them onto this helper if either is ever revived.)
function Resolve-RealProjectRoot {
    param([Parameter(Mandatory)][string]$Path)
    $marker = [regex]::Escape('.claude\worktrees\')
    if ($Path -match $marker) {
        $Path = $Path -replace ($marker + '[^\\]+$'), ''
        $Path = $Path.TrimEnd('\')
    }
    return $Path
}

# ============================================================================
# Configuration  -  mirrors dev-window-v2.ps1 Get-BuildSteps exactly
#
# SYNC RULE: The cmake configure args (flags, generator, paths) MUST match
# dev-window-v2.ps1 Get-BuildSteps(). If you change one, change the other.
# The VERSION_SEM_* flags are injected here and in Get-BuildSteps so both
# produce identical binaries given the same version string.
# ============================================================================

# Resolve project root from script location (devtools/ parent). Worktree builds are
# never allowed -- redirect to the real working copy via Resolve-RealProjectRoot.
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectDir = Split-Path -Parent $ScriptDir
$realProjectDir = Resolve-RealProjectRoot -Path $ProjectDir
if ($realProjectDir -ne $ProjectDir) {
    Write-Warning "Worktree path detected -- redirecting build to main working copy: $realProjectDir"
    $ProjectDir = $realProjectDir
}

# Unified build directory for client/updater/tests.
if ($OutputDir -ne "") {
    if ([System.IO.Path]::IsPathRooted($OutputDir)) {
        $BuildDir = $OutputDir
    } else {
        $BuildDir = Join-Path $ProjectDir $OutputDir
    }
} else {
    $BuildDir = Join-Path $ProjectDir "Build"
}
$StateFile  = Join-Path $BuildDir ".last_build_state.json"
if ($RetainHousekeeping) {
    . (Join-Path $ProjectDir 'tools/smoke-verify/lib/Storage-Harness.ps1')
    . (Join-Path $ProjectDir 'tools/smoke-verify/lib/Runner-Retention.ps1')
    Set-SmokeStorageRetention -RetainHousekeeping
    Initialize-SmokeStoragePolicy -Path (Join-Path $ProjectDir 'tools/smoke-verify/storage-policy.json')
}
Initialize-HeadlessRetainedContext
$AddinDir   = Join-Path $ProjectDir "..\post-batch-addin"
$PreferredCMake = "C:\msys64\mingw64\bin\cmake.exe"
if (Test-Path $PreferredCMake) {
    $CMakeExe = $PreferredCMake
} else {
    # Fallback to PATH if the pinned MSYS2 cmake is unavailable.
    $CMakeExe = "cmake"
}
$CC         = "C:/msys64/mingw64/bin/cc.exe"
$CXX        = "C:/msys64/mingw64/bin/c++.exe"
$PreferredPython = "C:/Python312/python.exe"
if (Test-Path -LiteralPath $PreferredPython) {
    # Codex desktop's sandbox can block MSYS Python before generated-header tools run.
    $PythonExe = $PreferredPython
} else {
    $PythonExe = "C:/msys64/usr/bin/python3.exe"
}
$NinjaExe   = "C:\msys64\mingw64\bin\ninja.exe"
$Generator  = "Ninja"

# ccache: injected via cmake launcher flags when the local toolchain can launch
# through it. Some desktop sandboxes allow ccache itself but hang when it spawns
# the compiler, so probe that path before wiring it into CMake.
# Install: pacman -S --noconfirm mingw-w64-x86_64-ccache
# NOTE: mold was installed (mold 2.40.4) but -fuse-ld=mold fails on MinGW because
# GCC looks for ld.mold.exe which doesn't exist (only mold.exe). Rolled back to GNU ld.
$CcacheExe = "C:\msys64\mingw64\bin\ccache.exe"
$CcacheLauncher = ""

function Resolve-BuildGitExecutable {
    $candidates = @(
        "C:\Program Files\Git\cmd\git.exe",
        "C:\Program Files\Git\bin\git.exe",
        "C:\msys64\mingw64\bin\git.exe",
        "C:\msys64\usr\bin\git.exe"
    )
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate) {
            return $candidate
        }
    }
    return ""
}

$GitExe = Resolve-BuildGitExecutable

# Build environment -- self-configures TEMP/TMP, PATH (MinGW64), MSYSTEM, ccache.
# Prelude is idempotent: safe to run multiple times or in nested script invocations.
. (Join-Path $ScriptDir "_build-env-prelude.ps1")
. (Join-Path $ScriptDir "version-util.ps1")

if ($GitExe -ne "") {
    Set-Alias -Name git -Value $GitExe -Scope Script
    $env:GIT_CONFIG_COUNT = "1"
    $env:GIT_CONFIG_KEY_0 = "safe.directory"
    $env:GIT_CONFIG_VALUE_0 = ([System.IO.Path]::GetFullPath($ProjectDir) -replace '\\', '/')
    $env:GIT_TERMINAL_PROMPT = "0"
} else {
    Write-Warning "Preferred Git executable not found; version metadata probes will fall back nonfatally."
}

if (-not $RetainHousekeeping -and (Test-Path -LiteralPath $CcacheExe)) {
    $probeOut = Join-Path $env:TEMP ("pd-ccache-probe-{0}.out" -f $PID)
    $probeErr = Join-Path $env:TEMP ("pd-ccache-probe-{0}.err" -f $PID)
    Remove-Item -LiteralPath $probeOut, $probeErr -Force -ErrorAction SilentlyContinue

    $probe = Start-Process -FilePath $CcacheExe `
                           -ArgumentList @($CC, "--version") `
                           -RedirectStandardOutput $probeOut `
                           -RedirectStandardError $probeErr `
                           -WindowStyle Hidden `
                           -PassThru
    # B-1280: Windows PowerShell 5.1 reports an empty ExitCode for a process
    # started this way unless its handle was opened before the process exited.
    # Reading Handle opens and caches it, so ExitCode is real after the wait.
    $null = $probe.Handle

    $probeExited = $probe.WaitForExit(5000)
    $probeExit = $null
    if ($probeExited) { $probeExit = $probe.ExitCode }
    if ($probeExited -and $probeExit -eq 0) {
        $CcacheLauncher = "-DCMAKE_C_COMPILER_LAUNCHER=`"$CcacheExe`" -DCMAKE_CXX_COMPILER_LAUNCHER=`"$CcacheExe`""
        Write-Host "ccache: compiler probe passed; building with the ccache launcher." -ForegroundColor DarkGray
    } else {
        if (-not $probe.HasExited) {
            Stop-Process -Id $probe.Id -Force -ErrorAction SilentlyContinue
        }
        $probeState = if ($probeExited) { "exit code '$probeExit'" } else { "no exit within 5 s" }
        Write-Warning "ccache compiler probe failed ($probeState); building without ccache launcher."
    }

    Remove-Item -LiteralPath $probeOut, $probeErr -Force -ErrorAction SilentlyContinue
}

if ($RetainHousekeeping) {
    $CcacheLauncher = "-DCMAKE_C_COMPILER_LAUNCHER= -DCMAKE_CXX_COMPILER_LAUNCHER="
}

$Cores = $env:NUMBER_OF_PROCESSORS
if (-not $Cores) { $Cores = 4 }

# ============================================================================
# Version resolution  -  mirrors dev-window-v2.ps1 Get-ProjectVersion
# If -Version "X.Y.Z" supplied, use it.
# If -UseNextVersion: max(CMake, git tags vX.Y.Z) then patch + 1, write CMakeLists.txt.
# Else read VERSION_SEM_* from CMakeLists.txt.
# ============================================================================

$VerMajor = 0; $VerMinor = 0; $VerPatch = 0

if ($Version -ne "") {
    $parts = $Version.Split('.')
    if ($parts.Count -eq 3) {
        $VerMajor = [int]$parts[0]
        $VerMinor = [int]$parts[1]
        $VerPatch = [int]$parts[2]
    } else {
        Write-Warning "-Version '$Version' is not in X.Y.Z format -- using 0.0.0"
    }
} elseif ($UseNextVersion) {
    $nextInfo = Get-NextReleaseSemVer $ProjectDir
    Write-Host "  [version] Next build version: $($nextInfo.NextString) (max of CMake + git tags was $($nextInfo.Previous.Major).$($nextInfo.Previous.Minor).$($nextInfo.Previous.Patch))" -ForegroundColor Cyan
    Set-CMakeListsSemVer $ProjectDir $nextInfo.Next.Major $nextInfo.Next.Minor $nextInfo.Next.Patch
    $VerMajor = $nextInfo.Next.Major
    $VerMinor = $nextInfo.Next.Minor
    $VerPatch = $nextInfo.Next.Patch
} else {
    # Read from CMakeLists.txt (same source as the Dev Window)
    $cmakeLists = Join-Path $ProjectDir "CMakeLists.txt"
    if (Test-Path $cmakeLists) {
        $cmakeContent = Get-Content $cmakeLists -Raw -ErrorAction SilentlyContinue
        if ($cmakeContent -match 'VERSION_SEM_MAJOR\s+(\d+)') { $VerMajor = [int]$Matches[1] }
        if ($cmakeContent -match 'VERSION_SEM_MINOR\s+(\d+)') { $VerMinor = [int]$Matches[1] }
        if ($cmakeContent -match 'VERSION_SEM_PATCH\s+(\d+)') { $VerPatch = [int]$Matches[1] }
    }
}

# SYNC: optional -DPD_STABLE_RELEASE=ON matches CMakeLists.txt (stable channel; omits PD_DEV_BUILD).
$vFlags = " -DVERSION_SEM_MAJOR=$VerMajor -DVERSION_SEM_MINOR=$VerMinor -DVERSION_SEM_PATCH=$VerPatch"
$gitFlag = ""
if ($GitExe -ne "") {
    $gitFlag = " -DPD_GIT_EXECUTABLE=`"$GitExe`""
}

# ============================================================================
# Console helpers
# ============================================================================

function Write-Header([string]$text) {
    $bar = "-" * 60
    Write-Host ""
    Write-Host $bar -ForegroundColor DarkGray
    Write-Host "  $text" -ForegroundColor Cyan
    Write-Host $bar -ForegroundColor DarkGray
}

function Write-Ok([string]$text)   { Write-Host $text -ForegroundColor Green }
function Write-Err([string]$text)  { Write-Host $text -ForegroundColor Red }
function Write-Info([string]$text) { Write-Host $text -ForegroundColor Gray }
function Write-Warn([string]$text) { Write-Host $text -ForegroundColor Yellow }

# SP-9 truncation guard (c069): factored out of the auto-commit path into named,
# unit-tested helpers (see -SelfTest). Get-Sp9FlagsFromNumstat is the PURE parser
# over `git diff --numstat` lines; Get-Sp9TruncationFlags runs it against the
# working tree. Flags any file whose diff-vs-HEAD is mostly-deletion (net < -20
# AND additions below a third of deletions) -- the AI-pipeline truncation
# signature, distinct from a legitimate large rewrite (which adds many lines too).
function Get-Sp9FlagsFromNumstat {
    param([string[]]$NumstatLines)
    $flagged = @()
    foreach ($numLine in $NumstatLines) {
        if ($numLine -match '^(\d+)\s+(\d+)\s+(.+)$') {
            $added     = [int]$Matches[1]
            $deleted   = [int]$Matches[2]
            $file      = $Matches[3].Trim()
            $net       = $added - $deleted
            $threshold = [Math]::Max(1, [Math]::Floor($deleted / 3))
            if ($net -lt -20 -and $added -lt $threshold) {
                $flagged += "    $file  (net ${net}: +$added / -$deleted)"
            }
        }
    }
    return $flagged   # callers wrap with @() for a correct .Count in all cases
}

function Get-Sp9TruncationFlags {
    param([Parameter(Mandatory)][string]$ProjectDir)
    $numstat = & git -C $ProjectDir diff HEAD --numstat 2>$null
    return (Get-Sp9FlagsFromNumstat -NumstatLines $numstat)
}

function Get-StepChildProcessIds([int]$parentPid) {
    $result = @()
    try {
        $children = @(Get-CimInstance Win32_Process -Filter "ParentProcessId=$parentPid" -ErrorAction Stop)
    } catch {
        try {
            $children = @(Get-WmiObject Win32_Process -Filter "ParentProcessId=$parentPid" -ErrorAction SilentlyContinue)
        } catch {
            $children = @()
        }
    }

    foreach ($child in $children) {
        $childPid = [int]$child.ProcessId
        $result += $childPid
        $result += Get-StepChildProcessIds $childPid
    }
    return $result
}

function Get-StepProcessRows([int]$rootPid) {
    if ($rootPid -le 0) { return @() }

    $ids = @($rootPid) + @(Get-StepChildProcessIds $rootPid)
    $rows = @()
    foreach ($pidValue in $ids) {
        try {
            $proc = Get-CimInstance Win32_Process -Filter "ProcessId=$pidValue" -ErrorAction Stop
            $cmd = [string]$proc.CommandLine
            if ($cmd.Length -gt 220) {
                $cmd = $cmd.Substring(0, 217) + "..."
            }
            $rows += [PSCustomObject]@{
                Pid = [int]$proc.ProcessId
                Parent = [int]$proc.ParentProcessId
                Name = [string]$proc.Name
                Command = $cmd
            }
            continue
        } catch {}

        try {
            $fallback = Get-Process -Id ([int]$pidValue) -ErrorAction Stop
            $cmd = ""
            try { $cmd = [string]$fallback.Path } catch {}
            if ($cmd -eq "") { $cmd = "<command line unavailable>" }
            if ($cmd.Length -gt 220) {
                $cmd = $cmd.Substring(0, 217) + "..."
            }
            $rows += [PSCustomObject]@{
                Pid = [int]$fallback.Id
                Parent = -1
                Name = [string]$fallback.ProcessName
                Command = $cmd
            }
        } catch {}
    }
    return $rows
}

function Write-BuildStepHeartbeat {
    param(
        [string]$StepName,
        [int]$RootPid,
        [DateTime]$Started,
        [string]$StdoutLog,
        [string]$StderrLog,
        [string]$HeartbeatLog
    )

    $elapsed = [math]::Floor(([DateTime]::Now - $Started).TotalSeconds)
    $stdoutBytes = if (Test-Path -LiteralPath $StdoutLog) { (Get-Item -LiteralPath $StdoutLog).Length } else { 0 }
    $stderrBytes = if (Test-Path -LiteralPath $StderrLog) { (Get-Item -LiteralPath $StderrLog).Length } else { 0 }
    $ninjaLog = Join-Path $BuildDir ".ninja_log"
    $ninjaText = if (Test-Path -LiteralPath $ninjaLog) {
        "ninja_log=$((Get-Item -LiteralPath $ninjaLog).Length)b"
    } else {
        "ninja_log=missing"
    }

    $header = "[{0}] {1} running {2}s pid={3} stdout={4}b stderr={5}b {6}" -f `
        ([DateTime]::Now.ToString("s")),
        $StepName,
        $elapsed,
        $RootPid,
        $stdoutBytes,
        $stderrBytes,
        $ninjaText
    Add-Content -LiteralPath $HeartbeatLog -Value $header -Encoding UTF8
    Write-Info ("  [heartbeat] {0}" -f $header)

    $rows = @(Get-StepProcessRows $RootPid)
    if ($rows.Count -eq 0) {
        Add-Content -LiteralPath $HeartbeatLog -Value "  (no live child process rows collected)" -Encoding UTF8
        return
    }

    foreach ($row in ($rows | Select-Object -First 12)) {
        Add-Content -LiteralPath $HeartbeatLog -Value ("  pid={0} ppid={1} {2} :: {3}" -f $row.Pid, $row.Parent, $row.Name, $row.Command) -Encoding UTF8
    }
    if ($rows.Count -gt 12) {
        Add-Content -LiteralPath $HeartbeatLog -Value ("  ... {0} more process row(s)" -f ($rows.Count - 12)) -Encoding UTF8
    }
}

function Get-SafeBuildStepLogName([string]$name) {
    $safe = $name -replace '[^A-Za-z0-9._-]+', '-'
    $safe = $safe.Trim([char[]]".-_")
    if ($safe -eq "") { $safe = "step" }
    return $safe.ToLowerInvariant()
}

# Returns $true if the line looks like a compiler/linker error
function Is-ErrorLine([string]$line) {
    return $line -match ':\s*error\s*:|:\s*fatal error\s*:|^make.*\*\*\*.*Error|^ninja.*\[\d+/\d+\].*FAILED|FAILED|undefined reference|multiple definition|collect2:\s*error|ld returned|cannot find -l|CMake Error|error:\s|Error:'
}

# ============================================================================
# Step runner  -  synchronous, captures and streams output
# ============================================================================

function Invoke-BuildStep {
    param(
        [string]$StepName,
        [string]$Exe,
        [string]$ArgList,
        [string]$WorkDir = $ProjectDir,
        [bool]  $ShowAll = $false   # $Verbose flag passed in
    )

    Write-Header $StepName

    # Ensure GCC has a writable temp dir; the system TEMP may point to a
    # restricted location (e.g. C:\Windows) in some sandbox environments.
    $goodTemp = if ($env:TEMP -and (Test-Path $env:TEMP)) { $env:TEMP } `
                else { "C:\Users\mikeh\AppData\Local\Temp" }

    if (-not (Test-Path -LiteralPath $BuildDir)) {
        New-Item -ItemType Directory -Path $BuildDir -Force | Out-Null
    }

    $logStamp = [DateTime]::Now.ToString("yyyyMMdd-HHmmss")
    $stepLogRoot = $BuildDir
    $retainedStepId = $null; $stepBinding = @{}
    if ($RetainHousekeeping) {
        [void](Assert-SmokeStoragePreflight -ProjectRoot $ProjectDir -Build)
        $retainedStepId = [guid]::NewGuid().ToString("N")
        $stepLogRoot = Join-Path $script:HeadlessRetainedRunRoot ("step-" + $retainedStepId)
        Assert-SmokePlainTree -Path $stepLogRoot
        New-Item -ItemType Directory -Path $stepLogRoot -ErrorAction Stop | Out-Null
    }
    $logName = Get-SafeBuildStepLogName $StepName
    $stepStdoutLog = Join-Path $stepLogRoot ("_build-headless-{0}-{1}.out.log" -f $logStamp, $logName)
    $stepStderrLog = Join-Path $stepLogRoot ("_build-headless-{0}-{1}.err.log" -f $logStamp, $logName)
    $stepExitLog = Join-Path $stepLogRoot ("_build-headless-{0}-{1}.exit" -f $logStamp, $logName)
    $stepHeartbeatLog = Join-Path $stepLogRoot ("_build-headless-{0}-{1}.heartbeat.log" -f $logStamp, $logName)
    $stepCmdLineLog = Join-Path $stepLogRoot ("_build-headless-{0}-{1}.cmdline.txt" -f $logStamp, $logName)
    if ($RetainHousekeeping) {
        $stepCmdLineLog = Join-Path $script:HeadlessRetainedRunRoot ('command-'+$retainedStepId+'.json')
        $stepExitLog = Join-Path $script:HeadlessRetainedRunRoot ('exit-'+$retainedStepId+'.json')
        $stepBinding = @{
            id=$retainedStepId; name=$StepName; executable=$Exe; arguments=$ArgList; working_directory=$WorkDir;
            source_sha256=$script:HeadlessRetainedSourceSha256; run_id=$script:HeadlessRetainedContext.RunId;
            owner=$script:HeadlessRetainedContext.Owner;
            stdout=$stepStdoutLog; stderr=$stepStderrLog; heartbeat=$stepHeartbeatLog;
            command=$stepCmdLineLog; exit=$stepExitLog
        }
        foreach ($path in @($stepStdoutLog,$stepStderrLog,$stepExitLog,$stepHeartbeatLog,$stepCmdLineLog)) {
            Assert-SmokePlainTree -Path $path
            if (Test-Path -LiteralPath $path) { throw 'Retained step output collision.' }
        }
        Write-HeadlessRetainedBytes $stepHeartbeatLog ([byte[]]@())
    } else {
    Remove-Item -LiteralPath $stepStdoutLog, $stepStderrLog, $stepExitLog, $stepCmdLineLog -Force -ErrorAction SilentlyContinue
    Set-Content -LiteralPath $stepHeartbeatLog -Value "" -Encoding UTF8
    }
    Write-Info "  [step-log] stdout: $stepStdoutLog"
    Write-Info "  [step-log] stderr: $stepStderrLog"
    Write-Info "  [step-log] heartbeat: $stepHeartbeatLog"
    Write-Info "  [step-log] exit:   $stepExitLog"
    Write-Info "  [step-log] cmdline: $stepCmdLineLog"

    $retainedOut = $null; $retainedErr = $null
    $retainedOutTask = $null; $retainedErrTask = $null
    $proc = $null; $retainedStepStarted = $false
    $stdoutLines = @()
    $stderrLines = @()
    $errorLines  = [System.Collections.Generic.List[string]]::new()

    try {
        $cmdLines = @(
            "WorkingDirectory=$WorkDir",
            "Executable=$Exe",
            "Arguments=$ArgList",
            "PATH=$env:PATH",
            "MSYSTEM=MINGW64",
            "MINGW_PREFIX=/mingw64",
            "TEMP=$goodTemp",
            "TMP=$goodTemp",
            "NINJA_STATUS=[%r/%f/%t] "
        )
        if ($RetainHousekeeping) {
            $receipt = Write-HeadlessRetainedReceipt ('command-'+$retainedStepId+'.json') ([Text.Encoding]::UTF8.GetBytes(($cmdLines -join "`r`n"))) $stepBinding
            $stepCmdLineLog = $receipt.Path
        } else {
            Set-Content -LiteralPath $stepCmdLineLog -Value $cmdLines -Encoding ASCII
        }

        $oldMsystem = $env:MSYSTEM
        $oldMingwPrefix = $env:MINGW_PREFIX
        $oldTemp = $env:TEMP
        $oldTmp = $env:TMP
        $oldNinjaStatus = $env:NINJA_STATUS

        $env:MSYSTEM = "MINGW64"
        $env:MINGW_PREFIX = "/mingw64"
        $env:TEMP = $goodTemp
        $env:TMP = $goodTemp
        $env:NINJA_STATUS = "[%r/%f/%t] "

        try {
            if ($RetainHousekeeping) {
                # Open both destinations exclusively before spawning. A raced file
                # refuses; native redirection never truncates a prior destination.
                $retainedOut = [IO.FileStream]::new($stepStdoutLog,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read,4096,$true)
                $retainedErr = [IO.FileStream]::new($stepStderrLog,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read,4096,$true)
                $start = New-Object Diagnostics.ProcessStartInfo
                $start.FileName=$Exe; $start.Arguments=$ArgList; $start.WorkingDirectory=$WorkDir
                $start.UseShellExecute=$false; $start.CreateNoWindow=$true
                $start.RedirectStandardOutput=$true; $start.RedirectStandardError=$true
                $proc=New-Object Diagnostics.Process; $proc.StartInfo=$start
                if (-not $proc.Start()) { throw 'Retained build step did not start.' }
                $retainedStepStarted = $true
                $stepBinding.child_pid = $proc.Id
                $stepBinding.child_creation_ticks = $proc.StartTime.ToUniversalTime().ToFileTimeUtc()
                $retainedOutTask=$proc.StandardOutput.BaseStream.CopyToAsync($retainedOut)
                $retainedErrTask=$proc.StandardError.BaseStream.CopyToAsync($retainedErr)
            } else {
            $proc = Start-Process -FilePath $Exe `
                                  -ArgumentList $ArgList `
                                  -WorkingDirectory $WorkDir `
                                  -RedirectStandardOutput $stepStdoutLog `
                                  -RedirectStandardError $stepStderrLog `
                                  -WindowStyle Hidden `
                                  -PassThru
            }
        } finally {
            $env:MSYSTEM = $oldMsystem
            $env:MINGW_PREFIX = $oldMingwPrefix
            $env:TEMP = $oldTemp
            $env:TMP = $oldTmp
            $env:NINJA_STATUS = $oldNinjaStatus
        }
    } catch {
        if ($RetainHousekeeping) {
            $launchFailure = $_.Exception.Message
            if ($retainedStepStarted -and -not $proc.HasExited) {
                throw 'Retained step launch/pipe failure: child closeout unknown; preserve parent lease and evidence.'
            }
            foreach ($stream in @($retainedOut,$retainedErr)) { if ($null -ne $stream) { $stream.Dispose() } }
            if ($null -ne $proc) { $proc.Dispose() }
            [void](Write-HeadlessRetainedReceipt ('launch-failure-'+$retainedStepId+'.json') ([Text.Encoding]::UTF8.GetBytes($launchFailure)) $stepBinding)
        }
        Write-Err "Failed to launch: $Exe $ArgList"
        Write-Err $_.Exception.Message
        return $false
    }

    if ($null -eq $proc) {
        Write-Err "Failed to launch: process handle was not created for $Exe $ArgList"
        return $false
    }

    # B-951 ROOT FIX (2026-07-05): cache the OS process handle NOW, while the process
    # is still alive. Start-Process -PassThru returns a Process whose .ExitCode reads
    # back as 0/null after the process exits UNLESS its Handle was retained during the
    # run (documented PowerShell behaviour). Without this, a failed ninja compile
    # (exit 1, "ninja: build stopped: subcommand failed") was captured as ExitCode 0,
    # the .exit file recorded 0, and the build reported SUCCESS while running a STALE
    # binary. Touching .Handle forces the SafeHandle to be cached so .ExitCode is
    # authoritative after WaitForExit. The prior B-951 patch (Refresh()+trust-non-zero,
    # below) could not help because .ExitCode was genuinely 0 -- the handle was gone.
    $procHandle = $null
    try { $procHandle = $proc.Handle } catch {}

    $spinChars = @('|', '/', '-', '\')
    $spinIdx   = 0
    $lastSpin  = [DateTime]::Now
    $lastHeartbeat = [DateTime]::MinValue

    while (-not $proc.WaitForExit(250)) {
        $now = [DateTime]::Now
        if (($now - $lastSpin).TotalMilliseconds -gt 5000) {
            $spin = $spinChars[$spinIdx % 4]
            $spinIdx++
            $elapsed = [math]::Floor(($now - $script:StepStart).TotalSeconds)
            Write-Host "`r  $spin  $StepName  (${elapsed}s)   " -NoNewline
            $lastSpin = $now
        }
        if (($now - $lastHeartbeat).TotalSeconds -ge 15) {
            Write-BuildStepHeartbeat -StepName $StepName `
                                     -RootPid $proc.Id `
                                     -Started $script:StepStart `
                                     -StdoutLog $stepStdoutLog `
                                     -StderrLog $stepStderrLog `
                                     -HeartbeatLog $stepHeartbeatLog
            $lastHeartbeat = $now
        }
    }

    $proc.WaitForExit()
    if ($RetainHousekeeping) {
        try {
            # Root exit alone is insufficient: both pumps must reach EOF.
            [void]$retainedOutTask.GetAwaiter().GetResult()
            [void]$retainedErrTask.GetAwaiter().GetResult()
            $retainedOut.Flush($true); $retainedErr.Flush($true)
        } finally {
            $retainedOut.Dispose(); $retainedErr.Dispose()
        }
        [void](Assert-SmokeStoragePreflight -ProjectRoot $ProjectDir -Build)
    }
    try {
        if ($RetainHousekeeping) {
            $receipt = Write-HeadlessRetainedReceipt ('exit-'+$retainedStepId+'.json') ([Text.Encoding]::ASCII.GetBytes([string][int]$proc.ExitCode)) $stepBinding
            $stepExitLog = $receipt.Path
        } else {
            Set-Content -LiteralPath $stepExitLog -Value ([string][int]$proc.ExitCode) -Encoding ASCII
        }
    } catch {
        if ($RetainHousekeeping) { throw }
        Write-Warn "Could not write step exit file for '$StepName': $stepExitLog"
        Write-Err $_.Exception.Message
    }

    if (Test-Path -LiteralPath $stepStdoutLog) {
        $stdoutLines = @(Get-Content -LiteralPath $stepStdoutLog -ErrorAction SilentlyContinue | Where-Object { $_ -ne "" })
    }
    if (Test-Path -LiteralPath $stepStderrLog) {
        $stderrLines = @(Get-Content -LiteralPath $stepStderrLog -ErrorAction SilentlyContinue | Where-Object { $_ -ne "" })
    }

    foreach ($outLine in $stdoutLines) {
        if ($ShowAll) {
            Write-Host $outLine
        } elseif (Is-ErrorLine $outLine) {
            Write-Err $outLine
            $errorLines.Add($outLine)
        }
    }

    foreach ($errLine in $stderrLines) {
        if ($ShowAll -or (Is-ErrorLine $errLine)) {
            if (Is-ErrorLine $errLine) {
                Write-Err $errLine
                $errorLines.Add($errLine)
            } else {
                Write-Host $errLine -ForegroundColor DarkGray
            }
        }
    }

    # Clear spinner line
    Write-Host "`r" + (" " * 72) + "`r" -NoNewline

    $exitCode = $null
    if ($RetainHousekeeping) {
        # The retained receipt is JSON; the cached, exited OS process is authoritative.
        $exitCode = [int]$proc.ExitCode
    } elseif (Test-Path -LiteralPath $stepExitLog) {
        $exitText = (Get-Content -LiteralPath $stepExitLog -Raw -ErrorAction SilentlyContinue).Trim()
        if ($exitText -match '^-?\d+$') {
            $exitCode = [int]$exitText
        }
    }
    # B-951: the .exit file has been observed to contain 0 while the child process
    # (ninja) actually FAILED ("ninja: build stopped: subcommand failed"), which
    # masked a broken compile as a SUCCESS build and ran a stale binary. A non-zero
    # OS-reported process ExitCode is authoritative -- if it disagrees with the file
    # and is non-zero, trust the process exit so the build correctly reports FAILED.
    try {
        $proc.Refresh()
        $procExit = $proc.ExitCode
        if ($null -ne $procExit -and [int]$procExit -ne 0 -and [int]$procExit -ne $exitCode) {
            Write-Warn "Step '$StepName': exit-file=$exitCode but process ExitCode=$procExit; trusting the non-zero process exit (B-951)."
            $exitCode = [int]$procExit
        }
    } catch {}
    if ($null -eq $exitCode) {
        try {
            $proc.Refresh()
            if ($null -ne $proc.ExitCode) {
                $exitCode = [int]$proc.ExitCode
            }
        } catch {}
    }
    if ($null -eq $exitCode) {
        $hasExitedText = "<unknown>"
        try { $hasExitedText = [string]$proc.HasExited } catch {}
        Write-Warn "Step exit code was not recorded for '$StepName'; treating it as failed. hasExited=$hasExitedText exitLog=$stepExitLog cmdline=$stepCmdLineLog"
        $exitCode = 1
    }

    # B-951 DEFENSE-IN-DEPTH (2026-07-05): even with the handle fix above, never let a
    # genuine build failure be reported as SUCCESS. ninja prints unambiguous failure
    # markers ("ninja: build stopped: subcommand failed", "FAILED: <target>"); if one
    # is present while the exit code looks clean, force FAILED. This backstops ANY
    # future exit-capture regression in this wrapper -- the class of bug that made the
    # first B-951 patch silently ineffective.
    if ($exitCode -eq 0) {
        $failMarker = @(@($stdoutLines) + @($stderrLines) | Where-Object {
            $_ -match 'ninja: build stopped' -or $_ -cmatch '^FAILED: '
        })
        if ($failMarker.Count -gt 0) {
            Write-Warn "Step '$StepName': exit code 0 but output shows a build failure (`"$($failMarker[0])`"); forcing FAILED (B-951 defense-in-depth)."
            $exitCode = 1
        }
    }

    try { $proc.Dispose() } catch {}

    $elapsed = [math]::Floor(([DateTime]::Now - $script:StepStart).TotalSeconds)

    if ($exitCode -ne 0) {
        Write-Err ""
        Write-Err ">>> $StepName FAILED (exit code $exitCode) after ${elapsed}s <<<"

        # Surface CMake diagnostic logs on configure failures
        if ($StepName -match "Configure") {
            $cmakeErrLog = Join-Path $BuildDir "CMakeFiles\CMakeError.log"
            $cmakeOutLog = Join-Path $BuildDir "CMakeFiles\CMakeConfigureLog.yaml"
            if (-not (Test-Path $cmakeOutLog)) {
                $cmakeOutLog = Join-Path $BuildDir "CMakeFiles\CMakeOutput.log"
            }
            foreach ($logFile in @($cmakeErrLog, $cmakeOutLog)) {
                if (Test-Path $logFile) {
                    Write-Warn ""
                    Write-Warn "--- $(Split-Path $logFile -Leaf) (last 40 lines) ---"
                    try {
                        Get-Content $logFile -Tail 40 | ForEach-Object { Write-Info "  $_" }
                    } catch {
                        Write-Info "  (could not read $logFile)"
                    }
                }
            }
        }

        # Print any captured error lines that weren't already shown
        if ($errorLines.Count -eq 0 -and -not $ShowAll) {
            # Show last 20 lines of stderr as context
            $tail = [math]::Min(20, $stderrLines.Count)
            if ($tail -gt 0) {
                Write-Warn ""
                Write-Warn "--- Last $tail lines of stderr ---"
                $stderrLines | Select-Object -Last $tail | ForEach-Object { Write-Info "  $_" }
            }
        }

        return $false
    }

    Write-Ok ">>> $StepName OK (${elapsed}s) <<<"
    return $true
}

function Invoke-BuildHeadlessSelfTest {
    Write-Header "Build Wrapper Self-Test"

    if ($OutputDir -eq "") {
        $script:BuildDir = Join-Path $ProjectDir ".claude\session-builds\build-headless-selftest-$PID"
    }
    if (Test-Path -LiteralPath $BuildDir) {
        Remove-Item -LiteralPath $BuildDir -Recurse -Force -ErrorAction SilentlyContinue
    }
    New-Item -ItemType Directory -Path $BuildDir -Force | Out-Null
    Write-Info "  Self-test dir: $BuildDir"

    $script:StepStart = [DateTime]::Now
    $warnOk = Invoke-BuildStep -StepName "SelfTest Warning Exit Zero" `
                               -Exe "$env:COMSPEC" `
                               -ArgList "/d /c `"echo selftest stdout && echo CMake Warning: benign warning 1>&2 && exit /b 0`"" `
                               -ShowAll $true

    $script:StepStart = [DateTime]::Now
    $failDetected = -not (Invoke-BuildStep -StepName "SelfTest Nonzero Exit" `
                                           -Exe "$env:COMSPEC" `
                                           -ArgList "/d /c `"echo fatal error: simulated failure 1>&2 && exit /b 7`"" `
                                           -ShowAll $true)

    $exitFiles = @(Get-ChildItem -LiteralPath $BuildDir -Filter "_build-headless-*.exit" -File -ErrorAction SilentlyContinue)
    $exitCodes = @()
    foreach ($file in $exitFiles) {
        $exitCodes += (Get-Content -LiteralPath $file.FullName -Raw -ErrorAction SilentlyContinue).Trim()
    }
    $exitOk = ($exitCodes -contains "0") -and ($exitCodes -contains "7")

    # SP-9 truncation-guard helper (c069): pure numstat parser unit checks.
    $sp9Flag  = @(Get-Sp9FlagsFromNumstat -NumstatLines @("5 100 foo.c"))          # net -95, +5 < floor(100/3)=33 -> FLAG
    $sp9Keep  = @(Get-Sp9FlagsFromNumstat -NumstatLines @("200 210 big.c"))        # net -10 (> -20) -> keep
    $sp9Gone  = @(Get-Sp9FlagsFromNumstat -NumstatLines @("0 50 gone.c"))          # net -50, +0 < 16 -> FLAG
    $sp9Multi = @(Get-Sp9FlagsFromNumstat -NumstatLines @("5 100 a.c","80 90 b.c","0 3 c.c"))  # only a.c
    $sp9Ok = ($sp9Flag.Count -eq 1) -and ($sp9Flag[0] -like "*foo.c*") -and `
             ($sp9Keep.Count -eq 0) -and ($sp9Gone.Count -eq 1) -and `
             ($sp9Multi.Count -eq 1) -and ($sp9Multi[0] -like "*a.c*")

    # Worktree-redirect resolver (c070): pure path guard unit checks.
    $wtRedir = Resolve-RealProjectRoot -Path 'C:\repo\.claude\worktrees\feature-x'
    $wtKeep  = Resolve-RealProjectRoot -Path 'C:\repo\sub\dir'
    $wtOk = ($wtRedir -eq 'C:\repo') -and ($wtKeep -eq 'C:\repo\sub\dir')

    if ($warnOk -and $failDetected -and $exitOk -and $sp9Ok -and $wtOk) {
        Write-Ok "  Build wrapper self-test passed."
        return $true
    }

    Write-Err "  Build wrapper self-test failed."
    Write-Err "  warnOk=$warnOk failDetected=$failDetected exitCodes=$($exitCodes -join ',') sp9Ok=$sp9Ok wtOk=$wtOk"
    return $false
}

if ($SelfTest) {
    if (Invoke-BuildHeadlessSelfTest) {
        exit 0
    }
    exit 1
}

# ============================================================================
# Smart clean-build detection
# Heuristics: force clean when (a) CMakeCache.txt missing, (b) generator
# changed, (c) compiler version changed, (d) -Clean flag.
# State is persisted in Build/.last_build_state.json.
# NOTE: branch changes intentionally do NOT trigger a clean — same compiler/
# generator on a different branch is an incremental build, not a full wipe.
# ============================================================================

function Get-CurrentBranch {
    try {
        $b = (& git -C $ProjectDir rev-parse --abbrev-ref HEAD 2>$null)
        if ($b) { return $b.Trim() }
    } catch {}
    return ""
}

function Read-BuildState {
    if (Test-Path $StateFile) {
        try { return (Get-Content $StateFile -Raw -ErrorAction Stop | ConvertFrom-Json) } catch {}
    }
    return $null
}

function Write-BuildState([string]$headHash) {
    $state = @{
        generator = $Generator
        compiler  = $CC
        branch    = (Get-CurrentBranch)
        builtHash = $headHash
    }
    $dir = Split-Path $StateFile -Parent
    if (-not (Test-Path $dir)) { New-Item -ItemType Directory -Path $dir -Force | Out-Null }
    if ($RetainHousekeeping) {
        Assert-SmokePlainTree -Path $StateFile
        if (Test-Path -LiteralPath $StateFile) {
            [void](Write-HeadlessRetainedReceipt ('state-before-' + [guid]::NewGuid().ToString('N') + '.json') ([IO.File]::ReadAllBytes($StateFile)))
        }
        [void](Assert-SmokeStoragePreflight -ProjectRoot $ProjectDir -Build)
    }
    try { $state | ConvertTo-Json -Depth 3 | Set-Content $StateFile -Encoding UTF8 -ErrorAction Stop }
    catch { if ($RetainHousekeeping) { throw } }
}

function Test-NeedsCleanBuild {
    # (e) Explicit -Clean flag
    if ($Clean) { return $true }

    # (a) CMakeCache.txt missing (fresh dir or someone deleted it)
    $cacheFile = Join-Path $BuildDir "CMakeCache.txt"
    if (-not (Test-Path $cacheFile)) { return $true }

    $state = Read-BuildState
    if ($null -eq $state) {
        # No state file: migrating from Unix Makefiles or first Ninja run
        Write-Info "  [smart-clean] No state file -- forcing clean (migration or first Ninja run)"
        return $true
    }

    # (b) Generator changed (e.g. migrating from Unix Makefiles)
    if ($state.generator -ne $Generator) {
        Write-Info "  [smart-clean] Generator changed ($($state.generator) -> $Generator) -- forcing clean"
        return $true
    }

    # (c) Compiler changed
    if ($state.compiler -ne $CC) {
        Write-Info "  [smart-clean] Compiler changed ($($state.compiler) -> $CC) -- forcing clean"
        return $true
    }

    return $false
}

# ============================================================================
# Main build flow
# ============================================================================

$script:StepStart = [DateTime]::Now
$totalStart = [DateTime]::Now

$currentHash = (& git -C $ProjectDir rev-parse HEAD 2>$null)
if ($currentHash) { $currentHash = $currentHash.Trim() }

Write-Host ""
Write-Host "  Perfect Dark PC Port  -  Headless Build" -ForegroundColor Cyan
Write-Host "  Target:     $Target" -ForegroundColor Gray
Write-Host "  Version:    $VerMajor.$VerMinor.$VerPatch" -ForegroundColor Gray
Write-Host "  Next ver:   $($UseNextVersion.IsPresent) (-UseNextVersion)" -ForegroundColor Gray
Write-Host "  Clean:      $($Clean.IsPresent)" -ForegroundColor Gray
Write-Host "  AutoCommit: $($AutoCommit.IsPresent) (-AutoCommit / -CommitPush)" -ForegroundColor Gray
Write-Host "  Verbose:    $($Verbose.IsPresent)" -ForegroundColor Gray
Write-Host "  Cores:      $Cores" -ForegroundColor Gray
Write-Host "  Generator:  $Generator" -ForegroundColor Gray
Write-Host "  BuildDir:   $BuildDir" -ForegroundColor DarkGray
Write-Host "  ProjectDir: $ProjectDir" -ForegroundColor DarkGray

# Validate tools
foreach ($tool in @($NinjaExe, "C:\msys64\mingw64\bin\cc.exe")) {
    if (-not (Test-Path $tool)) {
        Write-Err "Required tool not found: $tool"
        Write-Err "Install MSYS2/MinGW64 to C:\msys64 or adjust paths in build-headless.ps1"
        Write-Err "  Ninja:  pacman -S mingw-w64-x86_64-ninja"
        Write-Err "  ccache: pacman -S mingw-w64-x86_64-ccache"
        exit 1
    }
}

# ============================================================================
# Auto-Commit + Push (opt-in via -AutoCommit)
# ============================================================================

if ($AutoCommit) {
    Write-Header "Auto-Commit + Push"
    $lockFile = Join-Path $ProjectDir ".git\index.lock"
    if (Test-Path -LiteralPath $lockFile) {
        try { & cmd.exe /c "attrib -R `"$lockFile`"" 2>$null | Out-Null } catch {}
        Remove-Item -LiteralPath $lockFile -Force -ErrorAction SilentlyContinue
    }
    # dev.lock -- left behind by interrupted fetch/push or code sessions; delete silently
    foreach ($devLock in @(
        (Join-Path $ProjectDir ".git\refs\remotes\origin\dev.lock"),
        (Join-Path $ProjectDir "dev.lock")
    )) {
        if (Test-Path $devLock) { Remove-Item $devLock -Force -ErrorAction SilentlyContinue }
    }
    $commitMsg = "Build v$VerMajor.$VerMinor.$VerPatch - auto-commit before build"
    $stChanges = & git -C $ProjectDir status --porcelain 2>$null
    if ($stChanges) {
        # SP-9 truncation guard (see Get-Sp9TruncationFlags; unit-tested via -SelfTest).
        $flaggedFiles = @(Get-Sp9TruncationFlags -ProjectDir $ProjectDir)
        if ($flaggedFiles.Count -gt 0) {
            Write-Warn ""
            Write-Warn "  [SP-9 GUARD] Auto-commit SKIPPED -- unexpected file shrinkage:"
            $flaggedFiles | ForEach-Object { Write-Warn $_ }
            Write-Warn "  Verify file content against HEAD before committing."
            Write-Warn "  Restore: git -C `"$ProjectDir`" checkout HEAD -- <file>"
            Write-Info "  Build continues from working copy (commit was NOT made)."
        } else {
            & git -C $ProjectDir add -A 2>$null | Out-Null
            & git -C $ProjectDir commit -m $commitMsg 2>$null | Out-Null
            Write-Ok "  Committed: $commitMsg"
        }
    } else {
        Write-Info "  Nothing to commit."
    }
    # Push is non-fatal -- no internet or no remote won't abort the build
    try {
        & git -C $ProjectDir push 2>$null | Out-Null
        Write-Ok "  Pushed to remote."
    } catch {
        Write-Warn "  Push failed (non-fatal): $($_.Exception.Message)"
    }
} else {
    Write-Info ""
    Write-Info "  Auto-commit disabled (pass -AutoCommit to enable)."
}

# ============================================================================
# Stale build detection (informational only -- does not block)
# ============================================================================

$state = Read-BuildState
if ($state -and $state.builtHash -and $currentHash -and $state.builtHash -ne $currentHash) {
    $commitCount = (& git -C $ProjectDir rev-list --count "$($state.builtHash)..HEAD" 2>$null)
    if ($commitCount) { $commitCount = $commitCount.Trim() } else { $commitCount = "?" }
    Write-Warn ""
    Write-Warn "  WARNING: $commitCount new commit(s) since last successful build"
    Write-Warn "  Last built: $($state.builtHash)"
    Write-Warn "  Current:    $currentHash"
}

# ============================================================================
# Mods in the build dir survive a clean
# ============================================================================
# A clean removes the whole build directory. The game can keep its mods folder
# there ($E/mods): mods made with the in-game tools, mods a person dropped in,
# and the list of enabled mods. None of that is build output. It is moved to a
# folder beside the build directory for the wipe and moved back afterwards;
# nothing here ever deletes it. Dev mods (dev-mods/) are copied in again after
# the build, as before, on top of what was kept.

$script:BuildDirModItems = @("mods", "mods-enabled.json")

function Save-BuildDirMods([string]$Dir) {
    $present = @($script:BuildDirModItems | Where-Object { Test-Path -LiteralPath (Join-Path $Dir $_) })
    if ($present.Count -eq 0) { return $null }
    $keep = $Dir.TrimEnd('\', '/') + ".mods-keep"
    if (Test-Path -LiteralPath $keep) {
        # Left by a clean that was interrupted: it still holds mods. Never
        # write over it; stop so a person can put them back.
        throw "Mods from an interrupted clean are still in '$keep'. Move its contents back into '$Dir' and build again."
    }
    New-Item -ItemType Directory -Path $keep -Force | Out-Null
    foreach ($name in $present) {
        Move-Item -LiteralPath (Join-Path $Dir $name) -Destination (Join-Path $keep $name) -Force -ErrorAction Stop
    }
    Write-Info "  Kept aside for the clean: $($present -join ', ') -> $keep"
    return $keep
}

function Restore-BuildDirMods([string]$Dir, [string]$Keep) {
    if (-not $Keep) { return }
    if (-not (Test-Path -LiteralPath $Dir)) {
        New-Item -ItemType Directory -Path $Dir -Force | Out-Null
    }
    foreach ($name in $script:BuildDirModItems) {
        $src = Join-Path $Keep $name
        if (-not (Test-Path -LiteralPath $src)) { continue }
        Move-Item -LiteralPath $src -Destination (Join-Path $Dir $name) -Force -ErrorAction Stop
    }
    # Empty by now; without -Recurse this cannot remove anything that is left.
    Remove-Item -LiteralPath $Keep -Force -ErrorAction SilentlyContinue
    Write-Ok "  Mods put back into: $Dir"
}

# ============================================================================
# Smart clean (heuristic-based)
# ============================================================================

$needsClean = Test-NeedsCleanBuild
if ($needsClean) {
    $reason = if ($Clean) { "explicit -Clean flag" } else { "smart-clean heuristic (see above)" }
    if (Test-Path $BuildDir) {
        if ($RetainHousekeeping) {
            if (@(Get-ChildItem -LiteralPath $BuildDir -Force).Count) {
                throw 'Retained child refuses smart-clean of existing build bytes.'
            }
        } else {
        Write-Header "Cleaning Build Dir ($reason)"
        # Mods in this build's own mods folder are the player's work, not
        # build output: moved aside for the wipe, moved back after it.
        $modKeep = Save-BuildDirMods -Dir $BuildDir
        Remove-Item -Path $BuildDir -Recurse -Force -ErrorAction SilentlyContinue
        Write-Ok "  Cleaned: $BuildDir"
        Restore-BuildDirMods -Dir $BuildDir -Keep $modKeep
        }
    } else {
        Write-Info "  Build dir does not exist -- fresh build ($reason)"
    }
} else {
    Write-Info ""
    Write-Info "  [smart-clean] Incremental build (no clean needed)"
}

# Ensure build directory exists before configure/build phases.
if (-not (Test-Path $BuildDir)) {
    New-Item -ItemType Directory -Path $BuildDir -Force | Out-Null
    Write-Info "  Created build dir: $BuildDir"
}

# ============================================================================
# Updater signing key (B-1122 follow-up): a build never makes a keypair and
# never edits the tracked port/include/updater_pubkey.h. That header holds the
# release public key; a build without dev-keys/ (every fresh worktree) embeds
# it as a shipped client does, and the tree stays clean. A signing key is made
# only by release.ps1 when it has to sign, or by hand with keygen.ps1.
# ============================================================================

$devKeyPath = Join-Path $ProjectDir "dev-keys\ed25519-private.pem"
if (-not (Test-Path $devKeyPath)) {
    Write-Info "  No dev-keys signing key: this build embeds the tracked updater public key (signing is release.ps1's job)."
}

# ============================================================================
# CMake Configure
# ============================================================================

$configArgs = "-G $Generator -DCMAKE_C_COMPILER=`"$CC`" -DCMAKE_CXX_COMPILER=`"$CXX`" -DCMAKE_C_COMPILER_FORCED=TRUE -DCMAKE_CXX_COMPILER_FORCED=TRUE -DPD_PYTHON_EXECUTABLE=`"$PythonExe`" -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY $CcacheLauncher -B `"$BuildDir`" -S `"$ProjectDir`"$vFlags$gitFlag"

$script:StepStart = [DateTime]::Now
$configOk = Invoke-BuildStep -StepName "Configure (CMake - Ninja + ccache)" `
                              -Exe $CMakeExe `
                              -ArgList $configArgs `
                              -ShowAll $Verbose.IsPresent

if ($RetainHousekeeping -and $configOk) {
    Assert-HeadlessRetainedLaunchers $BuildDir $true
}

if (-not $configOk) {
    Write-Err ""
    Write-Err "Configure failed. Check CMake output above."
    exit 1
}

# ============================================================================
# Build targets
# ============================================================================

$targets = switch ($Target) {
    "client"  { @("client") }
    "updater" { @("updater") }
    "tests"   { @("tests") }
    "settings-tests" { @("settings-tests") }
    "probe"   { @("probe") }
    "all"     { @("client", "updater") }
}

$cmakeTargetMap = @{ "client" = "pd"; "updater" = "pd-updater"; "tests" = "pd-tests"; "settings-tests" = "pd-settings-save-tests"; "probe" = "scenario-scene-probe" }
$exeNameMap     = @{ "client" = "PerfectDark.exe"; "updater" = "Updater.exe"; "tests" = "pd-tests.exe"; "settings-tests" = "pd-settings-save-tests.exe"; "probe" = "scenario-scene-probe.exe" }

$results  = @{}
$anyFail  = $false

$script:StepStart = [DateTime]::Now
$headersOk = Invoke-BuildStep -StepName "Generate Headers [pd_headers]" `
                               -Exe $NinjaExe `
                               -ArgList "-C `"$BuildDir`" -j1 -v pd_headers" `
                               -ShowAll $Verbose.IsPresent
if (-not $headersOk) {
    Write-Err ""
    Write-Err "Generated-header step failed. Check the step stdout/stderr/heartbeat logs above."
    exit 1
}

foreach ($t in $targets) {
    $cmakeTarget = $cmakeTargetMap[$t]
    $label       = $t.Substring(0,1).ToUpper() + $t.Substring(1)
    $buildArgs   = "-C `"$BuildDir`" -v $cmakeTarget"
    if ($Jobs -gt 0) { $buildArgs += " -j$Jobs" }

    Write-Host ""
    Write-Host "============================================================" -ForegroundColor DarkCyan
    Write-Host "  Building: $label  (target: $cmakeTarget)" -ForegroundColor White
    Write-Host "  Dir:      $BuildDir" -ForegroundColor DarkGray
    Write-Host "============================================================" -ForegroundColor DarkCyan

    $tStart = [DateTime]::Now
    $script:StepStart = [DateTime]::Now
    $buildOk = Invoke-BuildStep -StepName "Compile [$label]" `
                                 -Exe $NinjaExe `
                                 -ArgList $buildArgs `
                                 -ShowAll $Verbose.IsPresent
    $tElapsed = [math]::Floor(([DateTime]::Now - $tStart).TotalSeconds)
    $results[$t] = @{ Ok = $buildOk; Elapsed = $tElapsed }
    if (-not $buildOk) { $anyFail = $true }

    # Post-build addin copy (client only)
    #
    # B-326 (2026-05-03): ROM file (*.z64) goes to install root, not
    # $BuildDir/data/. Post B-321 the binary's fsFileLoad searches at $E
    # (the EXE directory) with DEFAULT_BASEDIR_NAME=".", so any *.z64
    # under $BuildDir/data/ is dead bytes the client never reads.
    if ($buildOk -and $t -eq "client") {
        $dataDir = Join-Path $AddinDir "data"
        if (Test-Path $dataDir) {
            Write-Header "Post-Build: Copy Addin Files"
            try {
                # Place *.z64 anywhere in addin tree at install root.
                Get-ChildItem -Path $dataDir -Filter "*.z64" -Recurse -ErrorAction SilentlyContinue | ForEach-Object {
                    $destRom = Join-Path $BuildDir $_.Name
                    try { Copy-Item -Path $_.FullName -Destination $destRom -Force -ErrorAction Stop } catch {}
                }
                # Mirror data/ to $BuildDir/data/ but skip *.z64 so they do
                # not duplicate at the wrong location. Use robocopy for
                # filtered copy where available; fall back to file-walk.
                $dstData = Join-Path $BuildDir "data"
                if (-not (Test-Path $dstData)) { New-Item -ItemType Directory -Path $dstData -Force | Out-Null }
                $robocopy = Get-Command robocopy.exe -ErrorAction SilentlyContinue
                $copied = $false
                if ($null -ne $robocopy) {
                    & robocopy.exe $dataDir $dstData /E /XO /XF "*.z64" /NFL /NDL /NJH /NJS /NP | Out-Null
                    if ($LASTEXITCODE -le 7) { $copied = $true }
                }
                if (-not $copied) {
                    Get-ChildItem -Path $dataDir -Recurse -File -ErrorAction Stop | Where-Object { $_.Extension -ne ".z64" } | ForEach-Object {
                        $rel = $_.FullName.Substring($dataDir.Length).TrimStart('\','/')
                        $dest = Join-Path $dstData $rel
                        $destDir = Split-Path $dest -Parent
                        if (-not (Test-Path $destDir)) { New-Item -ItemType Directory -Path $destDir -Force | Out-Null }
                        Copy-Item -Path $_.FullName -Destination $dest -Force -ErrorAction SilentlyContinue
                    }
                }
                $hoistByor = Join-Path $dstData "put_your_rom_here.txt"
                $rootByor = Join-Path $BuildDir "put_your_rom_here.txt"
                if (Test-Path -LiteralPath $hoistByor) {
                    try { Move-Item -LiteralPath $hoistByor -Destination $rootByor -Force -ErrorAction Stop } catch {}
                }
                Write-Ok "  Copied addin\data -> $BuildDir (ROM at install root)"
            } catch {
                Write-Warn "  Addin copy failed (non-fatal): $($_.Exception.Message)"
            }
        }

        # Dev mods: copy selected git-tracked dev mods (dev-mods/<path>) into
        # <install>/mods so they survive clean builds. Selection via -DevMods:
        # "" => manifest "dev":true (default); "all"; "none"; "id1,id2".
        # See dev-mods/README.md. Non-fatal.
        $devModsManifest = Join-Path $ProjectDir "dev-mods\dev-mods.json"
        if (Test-Path -LiteralPath $devModsManifest) {
            try {
                $dm  = Get-Content -LiteralPath $devModsManifest -Raw | ConvertFrom-Json
                $sel = $DevMods.Trim().ToLower()
                if     ($sel -eq "none") { $names = @() }
                elseif ($sel -eq "all")  { $names = @($dm.mods | ForEach-Object { $_.id }) }
                elseif ($sel -ne "")     { $names = @($sel -split "," | ForEach-Object { $_.Trim() } | Where-Object { $_ }) }
                else                     { $names = @($dm.mods | Where-Object { $_.dev } | ForEach-Object { $_.id }) }

                if ($names.Count -gt 0) {
                    Write-Header "Post-Build: Copy Dev Mods"
                    $modsRoot = Join-Path $BuildDir "mods"
                    if (-not (Test-Path $modsRoot)) { New-Item -ItemType Directory -Path $modsRoot -Force | Out-Null }
                    $rc = Get-Command robocopy.exe -ErrorAction SilentlyContinue
                    foreach ($entry in $dm.mods) {
                        if ($names -notcontains $entry.id) { continue }
                        $src = Join-Path $ProjectDir (Join-Path "dev-mods" $entry.path)
                        if (-not (Test-Path -LiteralPath $src)) {
                            Write-Warn "  Dev mod '$($entry.id)' source missing: $src (skipped)"
                            continue
                        }
                        $dst = Join-Path $modsRoot $entry.path
                        if ($null -ne $rc) {
                            & robocopy.exe $src $dst /E /NFL /NDL /NJH /NJS /NP | Out-Null
                        } else {
                            if (-not (Test-Path $dst)) { New-Item -ItemType Directory -Path $dst -Force | Out-Null }
                            Copy-Item -Path (Join-Path $src "*") -Destination $dst -Recurse -Force -ErrorAction SilentlyContinue
                        }
                        Write-Ok "  Dev mod '$($entry.id)' -> $dst"
                    }
                } else {
                    Write-Info "  Dev mods: none selected (DevMods='$DevMods')"
                }
            } catch {
                Write-Warn "  Dev-mods copy failed (non-fatal): $($_.Exception.Message)"
            }
        }
    }
}

# ============================================================================
# Summary
# ============================================================================

$totalElapsed = [math]::Floor(([DateTime]::Now - $totalStart).TotalSeconds)

Write-Host ""
Write-Host "============================================================" -ForegroundColor DarkCyan
Write-Host "  BUILD SUMMARY" -ForegroundColor White
Write-Host "============================================================" -ForegroundColor DarkCyan

foreach ($t in $targets) {
    $r      = $results[$t]
    $status = if ($r.Ok) { "PASS" } else { "FAIL" }
    $color  = if ($r.Ok) { "Green" } else { "Red" }
    $exe    = $exeNameMap[$t]
    $path   = Join-Path $BuildDir $exe

    Write-Host ("  [{0,-6}]  {1,-8}  {2,4}s" -f $status, $t.ToUpper(), $r.Elapsed) -ForegroundColor $color
    if ($r.Ok -and (Test-Path $path)) {
        $size = [math]::Round((Get-Item $path).Length / 1MB, 1)
        Write-Host ("           -> {0}  ({1} MB)" -f $path, $size) -ForegroundColor DarkGray
    }
}

Write-Host ""
Write-Host ("  Total time: {0}s" -f $totalElapsed) -ForegroundColor Gray

if ($anyFail) {
    Write-Host "  Result: FAILED" -ForegroundColor Red
    Write-Host "============================================================" -ForegroundColor DarkCyan
    exit 1
} else {
    # Record build state so next run can detect clean/incremental correctly
    if ($currentHash) {
        Write-BuildState $currentHash
    }
    Write-Host "  Result: SUCCESS" -ForegroundColor Green
    Write-Host "============================================================" -ForegroundColor DarkCyan
    exit 0
}
