#Requires -Version 5.1
<#
.SYNOPSIS
    Headless build script for Perfect Dark PC Port  -  no GUI, pure console output.

.DESCRIPTION
    Runs the same CMake configure + build pipeline as dev-window-v2.ps1 but without
    any WinForms or windows. Suitable for CI, code sessions, and terminal use.
    Uses Ninja generator, unified Build/ directory, ccache, and mold linker.

.PARAMETER Target
    What to build: client, server, or all (default: all)

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
    .\build-headless.ps1 -Target server -Verbose
    .\build-headless.ps1 -AutoCommit
    powershell -File build-headless.ps1 -Target all -Clean
    .\build-headless.ps1 -OutputDir "Cursor Build"
    .\build-headless.ps1 -UseNextVersion -CommitPush
#>

param(
    [ValidateSet("client", "server", "all")]
    [string]$Target = "all",

    # Version override in "X.Y.Z" format. If omitted, reads VERSION_SEM_* from CMakeLists.txt
    # (same as what the Dev Window does with Get-ProjectVersion).
    [string]$Version = "",

    [switch]$Clean,

    [Alias("CommitPush")]
    [switch]$AutoCommit,

    [switch]$Verbose,

    [string]$OutputDir = "",

    [switch]$UseNextVersion
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

# Unified build directory (client + server share one dir -- no double-compile)
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
$CMakeExe   = "cmake"
$CC         = "C:/msys64/mingw64/bin/cc.exe"
$NinjaExe   = "C:\msys64\mingw64\bin\ninja.exe"
$Generator  = "Ninja"

# ccache: injected via cmake launcher flags
# Install: pacman -S --noconfirm mingw-w64-x86_64-ccache
# NOTE: mold was installed (mold 2.40.4) but -fuse-ld=mold fails on MinGW because
# GCC looks for ld.mold.exe which doesn't exist (only mold.exe). Rolled back to GNU ld.
$CcacheLauncher = "-DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache"

# Build environment -- self-configures TEMP/TMP, PATH (MinGW64), MSYSTEM, ccache.
# Prelude is idempotent: safe to run multiple times or in nested script invocations.
. (Join-Path $ScriptDir "_build-env-prelude.ps1")
. (Join-Path $ScriptDir "version-util.ps1")

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

$vFlags = " -DVERSION_SEM_MAJOR=$VerMajor -DVERSION_SEM_MINOR=$VerMinor -DVERSION_SEM_PATCH=$VerPatch"

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

    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName               = $Exe
    $psi.Arguments              = $ArgList
    $psi.WorkingDirectory       = $WorkDir
    $psi.UseShellExecute        = $false
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError  = $true
    $psi.CreateNoWindow         = $true
    $psi.EnvironmentVariables["PATH"]         = $env:PATH
    $psi.EnvironmentVariables["MSYSTEM"]      = "MINGW64"
    $psi.EnvironmentVariables["MINGW_PREFIX"] = "/mingw64"
    # Ensure GCC has a writable temp dir; the system TEMP may point to a
    # restricted location (e.g. C:\Windows) in some sandbox environments.
    $goodTemp = if ($env:TEMP -and (Test-Path $env:TEMP)) { $env:TEMP } `
                else { "C:\Users\mikeh\AppData\Local\Temp" }
    $psi.EnvironmentVariables["TEMP"]         = $goodTemp
    $psi.EnvironmentVariables["TMP"]          = $goodTemp

    $proc = New-Object System.Diagnostics.Process
    $proc.StartInfo = $psi

    $stdoutLines = [System.Collections.Generic.List[string]]::new()
    $stderrLines = [System.Collections.Generic.List[string]]::new()
    $errorLines  = [System.Collections.Generic.List[string]]::new()

    # Async readers to prevent deadlock when both stdout and stderr fill
    $stdoutQueue = [System.Collections.Concurrent.ConcurrentQueue[string]]::new()
    $stderrQueue = [System.Collections.Concurrent.ConcurrentQueue[string]]::new()

    try {
        [void]$proc.Start()
    } catch {
        Write-Err "Failed to launch: $Exe $ArgList"
        Write-Err $_.Exception.Message
        return $false
    }

    # Reader threads
    $stdoutReader = $proc.StandardOutput
    $stderrReader = $proc.StandardError

    $stdoutThread = [System.Threading.Thread]::new([System.Threading.ThreadStart]{
        try {
            $line = $stdoutReader.ReadLine()
            while ($null -ne $line) {
                $stdoutQueue.Enqueue($line)
                $line = $stdoutReader.ReadLine()
            }
        } catch {}
    })
    $stdoutThread.IsBackground = $true
    $stdoutThread.Start()

    $stderrThread = [System.Threading.Thread]::new([System.Threading.ThreadStart]{
        try {
            $line = $stderrReader.ReadLine()
            while ($null -ne $line) {
                $stderrQueue.Enqueue($line)
                $line = $stderrReader.ReadLine()
            }
        } catch {}
    })
    $stderrThread.IsBackground = $true
    $stderrThread.Start()

    # Drain both queues until process exits and both readers are done
    $spinChars = @('|', '/', '-', '\')
    $spinIdx   = 0
    $lastSpin  = [DateTime]::Now

    while (-not $proc.HasExited -or -not $stdoutQueue.IsEmpty -or -not $stderrQueue.IsEmpty) {
        $drained = $false
        $outLine = $null
        $errLine = $null

        while ($stdoutQueue.TryDequeue([ref]$outLine)) {
            $stdoutLines.Add($outLine)
            if ($ShowAll) {
                Write-Host $outLine
            } elseif (Is-ErrorLine $outLine) {
                Write-Err $outLine
                $errorLines.Add($outLine)
            }
            $drained = $true
        }

        while ($stderrQueue.TryDequeue([ref]$errLine)) {
            $stderrLines.Add($errLine)
            # stderr always shown (CMake progress + errors go here)
            if ($ShowAll -or (Is-ErrorLine $errLine)) {
                if (Is-ErrorLine $errLine) {
                    Write-Err $errLine
                    $errorLines.Add($errLine)
                } else {
                    Write-Host $errLine -ForegroundColor DarkGray
                }
            }
            $drained = $true
        }

        if (-not $drained) {
            # Spinner while waiting
            $now = [DateTime]::Now
            if (($now - $lastSpin).TotalMilliseconds -gt 250) {
                $spin = $spinChars[$spinIdx % 4]
                $spinIdx++
                $elapsed = [math]::Floor(($now - $script:StepStart).TotalSeconds)
                Write-Host "`r  $spin  $StepName  (${elapsed}s)   " -NoNewline
                $lastSpin = $now
            }
            [System.Threading.Thread]::Sleep(50)
        }
    }

    # Ensure reader threads finish
    $stdoutThread.Join(2000) | Out-Null
    $stderrThread.Join(2000) | Out-Null

    # Clear spinner line
    Write-Host "`r" + (" " * 72) + "`r" -NoNewline

    $exitCode = $proc.ExitCode
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

# ============================================================================
# Smart clean-build detection
# Heuristics: force clean when (a) CMakeCache.txt missing, (b) generator
# changed, (c) compiler version changed, (d) branch changed, (e) -Clean flag.
# State is persisted in Build/.last_build_state.json.
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

    # (d) Branch changed
    $currentBranch = Get-CurrentBranch
    if ($currentBranch -and $state.branch -and $currentBranch -ne $state.branch) {
        Write-Info "  [smart-clean] Branch changed ($($state.branch) -> $currentBranch) -- forcing clean"
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
    if (Test-Path $lockFile) { Remove-Item $lockFile -Force -ErrorAction SilentlyContinue }
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

# ============================================================================
# CMake Configure (unified Build/ dir for both pd and pd-server)
# ============================================================================

$configArgs = "-G $Generator -DCMAKE_C_COMPILER=`"$CC`" $CcacheLauncher -B `"$BuildDir`" -S `"$ProjectDir`"$vFlags"

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
    "client" { @("client") }
    "server" { @("server") }
    "all"    { @("client", "server") }
}

$cmakeTargetMap = @{ "client" = "pd"; "server" = "pd-server" }
$exeNameMap     = @{ "client" = "PerfectDark.exe"; "server" = "PerfectDarkServer.exe" }

$results  = @{}
$anyFail  = $false

foreach ($t in $targets) {
    $cmakeTarget = $cmakeTargetMap[$t]
    $label       = $t.Substring(0,1).ToUpper() + $t.Substring(1)
    $buildArgs   = "--build `"$BuildDir`" --target $cmakeTarget"

    Write-Host ""
    Write-Host "============================================================" -ForegroundColor DarkCyan
    Write-Host "  Building: $label  (target: $cmakeTarget)" -ForegroundColor White
    Write-Host "  Dir:      $BuildDir" -ForegroundColor DarkGray
    Write-Host "============================================================" -ForegroundColor DarkCyan

    $tStart = [DateTime]::Now
    $script:StepStart = [DateTime]::Now
    $buildOk = Invoke-BuildStep -StepName "Compile [$label]" `
                                 -Exe $CMakeExe `
                                 -ArgList $buildArgs `
                                 -ShowAll $Verbose.IsPresent
    $tElapsed = [math]::Floor(([DateTime]::Now - $tStart).TotalSeconds)
    $results[$t] = @{ Ok = $buildOk; Elapsed = $tElapsed }
    if (-not $buildOk) { $anyFail = $true }

    # Post-build addin copy (client only)
    if ($buildOk -and $t -eq "client") {
        $dataDir = Join-Path $AddinDir "data"
        if (Test-Path $dataDir) {
            Write-Header "Post-Build: Copy Addin Files"
            try {
                Copy-Item $dataDir -Destination $BuildDir -Recurse -Force
                Write-Ok "  Copied addin\data -> $BuildDir"
            } catch {
                Write-Warn "  Addin copy failed (non-fatal): $($_.Exception.Message)"
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
