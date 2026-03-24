#Requires -Version 5.1
<#
.SYNOPSIS
    Headless build script for Perfect Dark PC Port  -  no GUI, pure console output.

.DESCRIPTION
    Runs the same CMake configure + build pipeline as build-gui.ps1 but without
    any WinForms or windows. Suitable for CI, code sessions, and terminal use.

.PARAMETER Target
    What to build: client, server, or all (default: all)

.PARAMETER Clean
    Remove the build directory before configuring (clean build).

.PARAMETER Verbose
    Show full compiler output. Without this flag, only errors and summary lines
    are printed during the compile step.

.EXAMPLE
    .\build-headless.ps1
    .\build-headless.ps1 -Target client -Clean
    .\build-headless.ps1 -Target server -Verbose
    powershell -File build-headless.ps1 -Target all -Clean
#>

param(
    [ValidateSet("client", "server", "all")]
    [string]$Target = "all",

    [switch]$Clean,

    [switch]$Verbose
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

# ============================================================================
# Configuration  -  mirrors build-gui.ps1 exactly
# ============================================================================

$ProjectDir     = Split-Path -Parent $MyInvocation.MyCommand.Path
$ClientBuildDir = Join-Path $ProjectDir "build\client"
$ServerBuildDir = Join-Path $ProjectDir "build\server"
$AddinDir       = Join-Path $ProjectDir "..\post-batch-addin"
$CMakeExe       = "cmake"
$MakeExe        = "C:\msys64\usr\bin\make.exe"
$CC             = "C:/msys64/mingw64/bin/cc.exe"

# MSYS2 MINGW64 environment  -  same as GUI version
$env:MSYSTEM      = "MINGW64"
$env:MINGW_PREFIX = "/mingw64"
$env:PATH         = "C:\msys64\mingw64\bin;C:\msys64\usr\bin;$env:PATH"

$Cores = $env:NUMBER_OF_PROCESSORS
if (-not $Cores) { $Cores = 4 }

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
    return $line -match ':\s*error\s*:|:\s*fatal error\s*:|^make.*\*\*\*.*Error|FAILED|undefined reference|multiple definition|collect2:\s*error|ld returned|cannot find -l|CMake Error|error:\s|Error:'
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

    $proc = New-Object System.Diagnostics.Process
    $proc.StartInfo = $psi

    $stdoutLines = [System.Collections.Generic.List[string]]::new()
    $stderrLines = [System.Collections.Generic.List[string]]::new()
    $errorLines  = [System.Collections.Generic.List[string]]::new()

    # Async readers to prevent deadlock when both stdout and stderr fill
    $stdoutSb = [System.Text.StringBuilder]::new()
    $stderrSb = [System.Text.StringBuilder]::new()

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
            $cmakeErrLog = Join-Path $script:CurrentBuildDir "CMakeFiles\CMakeError.log"
            $cmakeOutLog = Join-Path $script:CurrentBuildDir "CMakeFiles\CMakeConfigureLog.yaml"
            if (-not (Test-Path $cmakeOutLog)) {
                $cmakeOutLog = Join-Path $script:CurrentBuildDir "CMakeFiles\CMakeOutput.log"
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
# Build a single target (client or server)
# ============================================================================

function Invoke-Target {
    param(
        [string]$TargetName   # "client" or "server"
    )

    $buildDir   = if ($TargetName -eq "server") { $ServerBuildDir } else { $ClientBuildDir }
    $makeTarget = if ($TargetName -eq "server") { "--target pd-server" } else { "--target pd" }
    $label      = if ($TargetName -eq "server") { "Server" } else { "Client" }

    $script:CurrentBuildDir = $buildDir

    Write-Host ""
    Write-Host "============================================================" -ForegroundColor DarkCyan
    Write-Host "  Building: $label" -ForegroundColor White
    Write-Host "  Dir:      $buildDir" -ForegroundColor DarkGray
    Write-Host "============================================================" -ForegroundColor DarkCyan

    # Clean
    if ($Clean) {
        if (Test-Path $buildDir) {
            Write-Info "  Cleaning build directory..."
            Remove-Item -Path $buildDir -Recurse -Force -ErrorAction SilentlyContinue
            Write-Ok "  Cleaned: $buildDir"
        } else {
            Write-Info "  Clean requested but directory does not exist  -  proceeding as fresh build."
        }
    }

    # Configure
    $configArgs = "-G `"Unix Makefiles`" -DCMAKE_MAKE_PROGRAM=`"$MakeExe`" -DCMAKE_C_COMPILER=`"$CC`" -B `"$buildDir`" -S `"$ProjectDir`""

    $script:StepStart = [DateTime]::Now
    $configOk = Invoke-BuildStep -StepName "Configure (CMake) [$label]" `
                                  -Exe $CMakeExe `
                                  -ArgList $configArgs `
                                  -ShowAll $Verbose.IsPresent

    if (-not $configOk) { return $false }

    # Build
    $buildArgs = "--build `"$buildDir`" $makeTarget -- -j$Cores -k"

    $script:StepStart = [DateTime]::Now
    $buildOk = Invoke-BuildStep -StepName "Compile [$label]" `
                                 -Exe $CMakeExe `
                                 -ArgList $buildArgs `
                                 -ShowAll $Verbose.IsPresent

    if (-not $buildOk) { return $false }

    # Post-build addin copy (client only)
    if ($TargetName -eq "client") {
        $dataDir = Join-Path $AddinDir "data"
        if (Test-Path $dataDir) {
            Write-Header "Post-Build: Copy Addin Files"
            try {
                Copy-Item $dataDir -Destination $buildDir -Recurse -Force
                Write-Ok "  Copied addin\data -> $buildDir"
            } catch {
                Write-Warn "  Addin copy failed (non-fatal): $($_.Exception.Message)"
            }
        }
    }

    return $true
}

# ============================================================================
# Main
# ============================================================================

$script:CurrentBuildDir = $ClientBuildDir
$script:StepStart       = [DateTime]::Now

$totalStart = [DateTime]::Now

Write-Host ""
Write-Host "  Perfect Dark PC Port  -  Headless Build" -ForegroundColor Cyan
Write-Host "  Target:  $Target" -ForegroundColor Gray
Write-Host "  Clean:   $($Clean.IsPresent)" -ForegroundColor Gray
Write-Host "  Verbose: $($Verbose.IsPresent)" -ForegroundColor Gray
Write-Host "  Cores:   $Cores" -ForegroundColor Gray
Write-Host "  ProjectDir: $ProjectDir" -ForegroundColor DarkGray

# Validate tools
foreach ($tool in @($MakeExe, "C:\msys64\mingw64\bin\cc.exe")) {
    if (-not (Test-Path $tool)) {
        Write-Err "Required tool not found: $tool"
        Write-Err "Install MSYS2/MinGW64 to C:\msys64 or adjust paths in build-headless.ps1"
        exit 1
    }
}

$targets = switch ($Target) {
    "client" { @("client") }
    "server" { @("server") }
    "all"    { @("client", "server") }
}

$results  = @{}
$anyFail  = $false

foreach ($t in $targets) {
    $tStart = [DateTime]::Now
    $ok = Invoke-Target -TargetName $t
    $tElapsed = [math]::Floor(([DateTime]::Now - $tStart).TotalSeconds)
    $results[$t] = @{ Ok = $ok; Elapsed = $tElapsed }
    if (-not $ok) { $anyFail = $true }
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
    $exe    = if ($t -eq "server") { "PerfectDarkServer.exe" } else { "PerfectDark.exe" }
    $dir    = if ($t -eq "server") { $ServerBuildDir } else { $ClientBuildDir }
    $path   = Join-Path $dir $exe

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
    Write-Host "  Result: SUCCESS" -ForegroundColor Green
    Write-Host "============================================================" -ForegroundColor DarkCyan
    exit 0
}
