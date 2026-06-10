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
    [ValidateSet("client", "updater", "tests", "probe", "all")]
    [string]$Target = "all",

    # Version override in "X.Y.Z" format. If omitted, reads VERSION_SEM_* from CMakeLists.txt
    # (same as what the Dev Window does with Get-ProjectVersion).
    [string]$Version = "",

    [switch]$Clean,

    [Alias("CommitPush")]
    [switch]$AutoCommit,

    [switch]$Verbose,

    [string]$OutputDir = "",

    [switch]$UseNextVersion,

    [switch]$SelfTest,

    # Dev-mod selection copied into <install>/mods. "" = manifest "dev":true
    # mods (default); "all" = every listed mod; "none" = no dev mods;
    # "id1,id2" = only the named mods. See dev-mods/README.md.
    [string]$DevMods = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

# ============================================================================
# Configuration  -  mirrors dev-window-v2.ps1 Get-BuildSteps exactly
#
# SYNC RULE: The cmake configure args (flags, generator, paths) MUST match
# dev-window-v2.ps1 Get-BuildSteps(). If you change one, change the other.
# The VERSION_SEM_* flags are injected here and in Get-BuildSteps so both
# produce identical binaries given the same version string.
# ============================================================================

# Resolve project root from script location (devtools/ parent).
# Guard: if running from inside a .claude/worktrees/ path, redirect to the
# real working copy. Worktree builds are NEVER allowed -- builds must operate
# on the main project files.
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectDir = Split-Path -Parent $ScriptDir
if ($ProjectDir -match [regex]::Escape('.claude\worktrees\')) {
    # Strip everything from .claude onward to get the real repo root
    $ProjectDir = $ProjectDir -replace ([regex]::Escape('.claude\worktrees\') + '[^\\]+$'), ''
    $ProjectDir = $ProjectDir.TrimEnd('\')
    Write-Warning "Worktree path detected -- redirecting build to main working copy: $ProjectDir"
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

if (Test-Path -LiteralPath $CcacheExe) {
    $probeOut = Join-Path $env:TEMP ("pd-ccache-probe-{0}.out" -f $PID)
    $probeErr = Join-Path $env:TEMP ("pd-ccache-probe-{0}.err" -f $PID)
    Remove-Item -LiteralPath $probeOut, $probeErr -Force -ErrorAction SilentlyContinue

    $probe = Start-Process -FilePath $CcacheExe `
                           -ArgumentList @($CC, "--version") `
                           -RedirectStandardOutput $probeOut `
                           -RedirectStandardError $probeErr `
                           -WindowStyle Hidden `
                           -PassThru

    if ($probe.WaitForExit(5000) -and $probe.ExitCode -eq 0) {
        $CcacheLauncher = "-DCMAKE_C_COMPILER_LAUNCHER=`"$CcacheExe`" -DCMAKE_CXX_COMPILER_LAUNCHER=`"$CcacheExe`""
    } else {
        if (-not $probe.HasExited) {
            Stop-Process -Id $probe.Id -Force -ErrorAction SilentlyContinue
        }
        Write-Warning "ccache compiler probe failed or timed out; building without ccache launcher."
    }

    Remove-Item -LiteralPath $probeOut, $probeErr -Force -ErrorAction SilentlyContinue
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
    $logName = Get-SafeBuildStepLogName $StepName
    $stepStdoutLog = Join-Path $BuildDir ("_build-headless-{0}-{1}.out.log" -f $logStamp, $logName)
    $stepStderrLog = Join-Path $BuildDir ("_build-headless-{0}-{1}.err.log" -f $logStamp, $logName)
    $stepExitLog = Join-Path $BuildDir ("_build-headless-{0}-{1}.exit" -f $logStamp, $logName)
    $stepHeartbeatLog = Join-Path $BuildDir ("_build-headless-{0}-{1}.heartbeat.log" -f $logStamp, $logName)
    $stepCmdLineLog = Join-Path $BuildDir ("_build-headless-{0}-{1}.cmdline.txt" -f $logStamp, $logName)
    Remove-Item -LiteralPath $stepStdoutLog, $stepStderrLog, $stepExitLog, $stepCmdLineLog -Force -ErrorAction SilentlyContinue
    Set-Content -LiteralPath $stepHeartbeatLog -Value "" -Encoding UTF8
    Write-Info "  [step-log] stdout: $stepStdoutLog"
    Write-Info "  [step-log] stderr: $stepStderrLog"
    Write-Info "  [step-log] heartbeat: $stepHeartbeatLog"
    Write-Info "  [step-log] exit:   $stepExitLog"
    Write-Info "  [step-log] cmdline: $stepCmdLineLog"

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
        Set-Content -LiteralPath $stepCmdLineLog -Value $cmdLines -Encoding ASCII

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
            $proc = Start-Process -FilePath $Exe `
                                  -ArgumentList $ArgList `
                                  -WorkingDirectory $WorkDir `
                                  -RedirectStandardOutput $stepStdoutLog `
                                  -RedirectStandardError $stepStderrLog `
                                  -WindowStyle Hidden `
                                  -PassThru
        } finally {
            $env:MSYSTEM = $oldMsystem
            $env:MINGW_PREFIX = $oldMingwPrefix
            $env:TEMP = $oldTemp
            $env:TMP = $oldTmp
            $env:NINJA_STATUS = $oldNinjaStatus
        }
    } catch {
        Write-Err "Failed to launch: $Exe $ArgList"
        Write-Err $_.Exception.Message
        return $false
    }

    if ($null -eq $proc) {
        Write-Err "Failed to launch: process handle was not created for $Exe $ArgList"
        return $false
    }

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
    try {
        Set-Content -LiteralPath $stepExitLog -Value ([string][int]$proc.ExitCode) -Encoding ASCII
    } catch {
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
    if (Test-Path -LiteralPath $stepExitLog) {
        $exitText = (Get-Content -LiteralPath $stepExitLog -Raw -ErrorAction SilentlyContinue).Trim()
        if ($exitText -match '^-?\d+$') {
            $exitCode = [int]$exitText
        }
    }
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

    if ($warnOk -and $failDetected -and $exitOk) {
        Write-Ok "  Build wrapper self-test passed."
        return $true
    }

    Write-Err "  Build wrapper self-test failed."
    Write-Err "  warnOk=$warnOk failDetected=$failDetected exitCodes=$($exitCodes -join ',')"
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
    try { $state | ConvertTo-Json -Depth 3 | Set-Content $StateFile -Encoding UTF8 -ErrorAction Stop } catch {}
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
        # SP-9 truncation guard: flag any file where net line delta < -20 AND
        # additions < 1/3 of deletions.  That pattern (mostly-deleted, few-added)
        # matches every known AI-pipeline truncation incident; it does NOT match
        # legitimate large rewrites (which have high additions too).
        $numstatOut  = & git -C $ProjectDir diff HEAD --numstat 2>$null
        $flaggedFiles = @()
        foreach ($numLine in $numstatOut) {
            if ($numLine -match '^(\d+)\s+(\d+)\s+(.+)$') {
                $added    = [int]$Matches[1]
                $deleted  = [int]$Matches[2]
                $file     = $Matches[3].Trim()
                $net      = $added - $deleted
                $threshold = [Math]::Max(1, [Math]::Floor($deleted / 3))
                if ($net -lt -20 -and $added -lt $threshold) {
                    $flaggedFiles += "    $file  (net ${net}: +$added / -$deleted)"
                }
            }
        }
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
# Smart clean (heuristic-based)
# ============================================================================

$needsClean = Test-NeedsCleanBuild
if ($needsClean) {
    $reason = if ($Clean) { "explicit -Clean flag" } else { "smart-clean heuristic (see above)" }
    if (Test-Path $BuildDir) {
        Write-Header "Cleaning Build Dir ($reason)"
        Remove-Item -Path $BuildDir -Recurse -Force -ErrorAction SilentlyContinue
        Write-Ok "  Cleaned: $BuildDir"
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
# Auto-generate Ed25519 keypair if not present (idempotent)
# ============================================================================

$devKeyPath = Join-Path $ProjectDir "dev-keys\ed25519-private.pem"
if (-not (Test-Path $devKeyPath)) {
    Write-Header "Generating Ed25519 keypair (first build)"
    $keygenScript = Join-Path $ScriptDir "keygen.ps1"
    if (Test-Path $keygenScript) {
        & $keygenScript
        if ($LASTEXITCODE -ne 0) {
            Write-Warn "  Keypair generation failed -- build continues but releases won't be signable."
        }
    } else {
        Write-Warn "  $keygenScript not found -- skipping key generation."
    }
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
    "probe"   { @("probe") }
    "all"     { @("client", "updater") }
}

$cmakeTargetMap = @{ "client" = "pd"; "updater" = "pd-updater"; "tests" = "pd-tests"; "probe" = "scenario-scene-probe" }
$exeNameMap     = @{ "client" = "PerfectDark.exe"; "updater" = "Updater.exe"; "tests" = "pd-tests.exe"; "probe" = "scenario-scene-probe.exe" }

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
