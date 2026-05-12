#Requires -Version 5.1
<#
.SYNOPSIS
    Smoke verify gate runner (Phase 1).

.DESCRIPTION
    Drives PerfectDark.exe through a scripted scenario using --smoke,
    captures the resulting pd-client.log, and applies the test JSON's
    declared assertions.

    Designed to plug into the build-headless flow; can also run
    standalone for local verification.

.PARAMETER Test
    Run a single test by file stem (e.g. -Test boot_smoke). Repeatable.

.PARAMETER Tag
    Run all tests carrying this tag. Repeatable.

.PARAMETER AutoSelect
    Run only tests whose paths_of_interest match a changed path between
    -MergeBase and HEAD. Falls back to all tests if git is unavailable.

.PARAMETER MergeBase
    Git reference used as the base for -AutoSelect. Default: dev.

.PARAMETER Build
    Run the queued build (devtools/build-session.ps1) before running
    tests. Uses the -Session value (default: smoke-<utc>).

.PARAMETER Session
    Session id passed to the queued build. Default: smoke-<utc>.

.PARAMETER Install
    Use an existing install directory instead of a per-test fresh copy.
    Implies -Keep; never used in CI.

.PARAMETER Keep
    Do not delete the per-run dir on success.

.PARAMETER Verbose
    Print every assertion check, not just failures.

.PARAMETER Timeout
    Override timeout_seconds in the test definitions. Use for CI where
    the budget needs a higher floor.

.PARAMETER TestsDir
    Where to discover tests. Default: tools/smoke-verify/tests.

.EXAMPLE
    .\tools\smoke-verify\run.ps1 -Test boot_smoke -Verbose

.EXAMPLE
    .\tools\smoke-verify\run.ps1 -AutoSelect -MergeBase dev

.EXAMPLE
    .\tools\smoke-verify\run.ps1 -Tag stability -Tag boot
#>

[CmdletBinding()]
param(
    [string[]] $Test,
    [string[]] $Tag,
    [switch]   $AutoSelect,
    [string]   $MergeBase = "dev",

    [switch]   $Build,
    [string]   $Session = "",

    [string]   $Install = "",
    [switch]   $Keep,
    [int]      $Timeout = 0,

    [string]   $TestsDir = "",
    [string]   $SourceBinary = "",
    [string]   $SourceRom = "",
    [switch]   $VerboseAssertions
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

# ----------------------------------------------------------------
# Bootstrap paths
# ----------------------------------------------------------------

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectRoot = Split-Path -Parent (Split-Path -Parent $ScriptDir)
$LibDir = Join-Path $ScriptDir "lib"
if (-not $TestsDir) { $TestsDir = Join-Path $ScriptDir "tests" }

$RunRoot = Join-Path $ProjectRoot ".claude\smoke-verify-runs"
$ResultsFile = ""

. (Join-Path $LibDir "Test-Assertions.ps1")
. (Join-Path $LibDir "Install-Harness.ps1")

function Write-Info([string]$t) { Write-Host $t -ForegroundColor Gray }
function Write-Ok([string]$t)   { Write-Host $t -ForegroundColor Green }
function Write-Warn([string]$t) { Write-Host $t -ForegroundColor Yellow }
function Write-Fail([string]$t) { Write-Host $t -ForegroundColor Red }

# ----------------------------------------------------------------
# Helper: enumerate tests
# ----------------------------------------------------------------

function Get-SmokeTests {
    [CmdletBinding()] param([Parameter(Mandatory)] [string] $Dir)
    if (-not (Test-Path -LiteralPath $Dir)) {
        throw "Tests directory not found: $Dir"
    }
    # Recurse so bug-regression tests at tests/bugs/B-NNN.json are
    # discovered alongside scenario tests at tests/*.json. Schema is the
    # same; bug-regression tests carry an extra bug_id field that the
    # runner surfaces in results for the bug-tracker auto-flip path.
    $files = Get-ChildItem -LiteralPath $Dir -Filter "*.json" -File -Recurse -ErrorAction SilentlyContinue
    $out = @()
    foreach ($f in $files) {
        try {
            $raw = Get-Content -LiteralPath $f.FullName -Raw
            # Strip // line comments because JSON.NET does not accept them.
            $clean = ($raw -replace '(?m)^\s*//.*$', '')
            $def = $clean | ConvertFrom-Json
        } catch {
            Write-Warn ("Failed to parse {0}: {1}" -f $f.Name, $_.Exception.Message)
            continue
        }

        # Compute a stable category from the path relative to $Dir so the
        # bug tracker can group results (e.g. "scenario" or "bugs").
        $rootFull = (Resolve-Path -LiteralPath $Dir).Path.TrimEnd('\').TrimEnd('/')
        $fileFull = $f.FullName
        $rel = $fileFull
        if ($fileFull.StartsWith($rootFull, [System.StringComparison]::OrdinalIgnoreCase)) {
            $rel = $fileFull.Substring($rootFull.Length).TrimStart('\').TrimStart('/')
        }
        $relParent = Split-Path -Parent $rel
        if (-not $relParent -or $relParent -eq "" -or $relParent -eq ".") {
            $category = "scenario"
        } else {
            $category = $relParent.Replace("\", "/")
        }

        $bugId = $null
        if ($def.PSObject.Properties.Match('bug_id').Count -gt 0 -and $def.bug_id) {
            $bugId = [string]$def.bug_id
        }
        $regressionFor = $null
        if ($def.PSObject.Properties.Match('regression_for').Count -gt 0 -and $def.regression_for) {
            $regressionFor = [string]$def.regression_for
        }

        $out += [PSCustomObject]@{
            Name = [System.IO.Path]::GetFileNameWithoutExtension($f.Name)
            Path = $f.FullName
            Category = $category
            BugId = $bugId
            RegressionFor = $regressionFor
            Definition = $def
        }
    }
    return $out
}

function Get-ChangedPaths {
    [CmdletBinding()] param([Parameter(Mandatory)] [string] $Base)
    try {
        $gitOut = & git -C $ProjectRoot diff --name-only "$Base...HEAD" 2>$null
        if ($LASTEXITCODE -ne 0) { return @() }
        return @($gitOut | Where-Object { $_ -ne "" })
    } catch {
        return @()
    }
}

function Test-MatchesPath {
    [CmdletBinding()] param(
        [Parameter(Mandatory)] [string[]] $Patterns,
        [Parameter(Mandatory)] [string[]] $Paths
    )
    foreach ($p in $Paths) {
        foreach ($pat in $Patterns) {
            if (-not $pat) { continue }
            # Convert a glob-ish pattern to a -like form. The test
            # definitions use simple */** style globs.
            if ($p -like $pat) { return $true }
        }
    }
    return $false
}

function Select-SmokeTests {
    [CmdletBinding()] param(
        [Parameter(Mandatory)] [array] $All,
        [string[]] $Names,
        [string[]] $Tags,
        [switch] $UseAutoSelect,
        [string] $AutoSelectBase = "dev"
    )

    $candidates = $All
    if ($Names -and $Names.Count -gt 0) {
        $candidates = @($candidates | Where-Object { $Names -contains $_.Name })
    }
    if ($Tags -and $Tags.Count -gt 0) {
        $candidates = @($candidates | Where-Object {
            $defTags = @()
            if ($_.Definition.PSObject.Properties.Match('tags').Count -gt 0) {
                $defTags = @($_.Definition.tags)
            }
            foreach ($t in $Tags) {
                if ($defTags -contains $t) { return $true }
            }
            return $false
        })
    }
    if ($UseAutoSelect) {
        $changed = Get-ChangedPaths -Base $AutoSelectBase
        if ($changed.Count -eq 0) {
            Write-Info "Auto-select: no changed paths discovered (or git unavailable); running all tests."
        } else {
            Write-Info ("Auto-select: {0} changed paths discovered vs {1}." -f $changed.Count, $AutoSelectBase)
            $candidates = @($candidates | Where-Object {
                $patterns = @()
                if ($_.Definition.PSObject.Properties.Match('paths_of_interest').Count -gt 0) {
                    $patterns = @($_.Definition.paths_of_interest)
                }
                if ($patterns.Count -eq 0) { return $true }  # no filter -> always eligible
                Test-MatchesPath -Patterns $patterns -Paths $changed
            })
        }
    }
    return $candidates
}

# ----------------------------------------------------------------
# Run one test
# ----------------------------------------------------------------

function Invoke-SmokeTest {
    [CmdletBinding()] param(
        [Parameter(Mandatory)] [psobject] $Test,
        [string] $ExistingInstall = "",
        [switch] $KeepOnSuccess,
        [int] $TimeoutOverride = 0,
        [switch] $VerboseEval,
        [string] $BinaryOverride = "",
        [string] $RomOverride = ""
    )

    $name = $Test.Name
    $def = $Test.Definition
    Write-Host ""
    Write-Host ("=== {0} ===" -f $name) -ForegroundColor Cyan
    if ($def.PSObject.Properties.Match('description').Count -gt 0) {
        Write-Info ("  {0}" -f $def.description)
    }

    $installState = "clean"
    if ($def.PSObject.Properties.Match('install_state').Count -gt 0 -and $def.install_state) {
        $installState = [string]$def.install_state
    }

    if (-not (Test-Path -LiteralPath $RunRoot)) {
        New-Item -ItemType Directory -Path $RunRoot -Force | Out-Null
    }

    $installInfo = $null
    if ($ExistingInstall) {
        $installInfo = [PSCustomObject]@{
            InstallDir = (Resolve-Path -LiteralPath $ExistingInstall).Path
            SourceBinary = ""
            SourceRom = ""
            RomId = ""
            InstallState = "current"
        }
    } else {
        $installInfo = New-SmokeInstall `
            -RunRoot $RunRoot `
            -TestName $name `
            -InstallState $installState `
            -SourceBinary $BinaryOverride `
            -SourceRom $RomOverride `
            -ProjectRoot $ProjectRoot
    }

    Write-Info ("  install dir: {0}" -f $installInfo.InstallDir)
    Write-Info ("  state: {0}" -f $installInfo.InstallState)

    # Resolve timeout
    $timeoutSeconds = 90
    if ($def.PSObject.Properties.Match('timeout_seconds').Count -gt 0 -and $def.timeout_seconds) {
        $timeoutSeconds = [int]$def.timeout_seconds
    }
    if ($TimeoutOverride -gt 0) { $timeoutSeconds = $TimeoutOverride }
    # 60 second cushion beyond the harness's own timeout so the harness
    # is the side that fires "result=timeout", not the runner.
    $watchdogSeconds = $timeoutSeconds + 60

    # Assemble process args
    $exe = Join-Path $installInfo.InstallDir "PerfectDark.exe"
    if (-not (Test-Path -LiteralPath $exe)) {
        throw "PerfectDark.exe missing inside install dir after seeding: $exe"
    }

    $bootArgs = @()
    if ($def.PSObject.Properties.Match('boot_args').Count -gt 0) {
        $bootArgs = @($def.boot_args)
    }
    $allArgs = @("--smoke", $Test.Path, "--no-crash-handler") + $bootArgs

    $started = Get-Date
    $proc = $null
    $exitCode = -1
    try {
        $proc = Start-Process -FilePath $exe `
            -ArgumentList $allArgs `
            -WorkingDirectory $installInfo.InstallDir `
            -PassThru `
            -NoNewWindow:$false `
            -WindowStyle Hidden
        if (-not $proc.WaitForExit($watchdogSeconds * 1000)) {
            Write-Warn ("Watchdog firing after {0}s; terminating PerfectDark.exe (pid {1})." -f $watchdogSeconds, $proc.Id)
            try { $proc.Kill() } catch {}
            try { $proc.WaitForExit(5000) | Out-Null } catch {}
            $exitCode = -2
        } else {
            $exitCode = $proc.ExitCode
        }
    } catch {
        Write-Fail ("Failed to launch PerfectDark.exe: {0}" -f $_.Exception.Message)
        $exitCode = -3
    }
    $elapsed = ((Get-Date) - $started).TotalSeconds

    $logPath = Get-SmokeLogPath -InstallDir $installInfo.InstallDir
    Write-Info ("  log: {0}" -f $logPath)
    Write-Info ("  exit code: {0}" -f $exitCode)
    Write-Info ("  elapsed: {0:N1}s" -f $elapsed)

    # Run assertions
    $assertions = $null
    if ($def.PSObject.Properties.Match('assertions').Count -gt 0) {
        $assertions = $def.assertions
    } else {
        $assertions = [PSCustomObject]@{}
    }

    $assertResult = Invoke-SmokeAssertions -LogPath $logPath -Assertions $assertions -Verbose:$VerboseEval

    $testOk = $assertResult.Passed -and ($exitCode -eq 0)
    $reasonExitNonZero = $false
    if ($exitCode -ne 0 -and $assertResult.Passed) {
        # The runner expects the harness to exit 0 on scripted-exit. A non-zero
        # exit when assertions pass suggests timeout-fired or the binary
        # crashed; treat as a failure but log it explicitly.
        $reasonExitNonZero = $true
        $testOk = $false
    }

    foreach ($line in (Format-AssertionFailures -Result $assertResult)) {
        if ($testOk) { Write-Info $line } else { Write-Fail $line }
    }
    if ($reasonExitNonZero) {
        Write-Fail "  exit code non-zero: harness force-exited (timeout or fatal)"
    }

    if ($testOk -and -not $KeepOnSuccess -and -not $ExistingInstall) {
        try {
            Remove-Item -LiteralPath $installInfo.InstallDir -Recurse -Force -ErrorAction Stop
            Write-Info "  cleaned install dir"
        } catch {
            Write-Warn ("  cleanup skipped: {0}" -f $_.Exception.Message)
        }
    } elseif (-not $testOk) {
        Write-Info ("  retained for debugging: {0}" -f $installInfo.InstallDir)
        try {
            $tail = Get-Content -LiteralPath $logPath -Tail 30 -ErrorAction SilentlyContinue
            if ($tail) {
                Write-Host "  --- log tail (last 30 lines) ---" -ForegroundColor DarkGray
                foreach ($t in $tail) { Write-Host ("    {0}" -f $t) -ForegroundColor DarkGray }
            }
        } catch {}
    }

    $bugId = $null
    if ($Test.PSObject.Properties.Match('BugId').Count -gt 0) { $bugId = $Test.BugId }
    $regressionFor = $null
    if ($Test.PSObject.Properties.Match('RegressionFor').Count -gt 0) { $regressionFor = $Test.RegressionFor }
    $category = "scenario"
    if ($Test.PSObject.Properties.Match('Category').Count -gt 0 -and $Test.Category) { $category = $Test.Category }

    return [PSCustomObject]@{
        Name = $name
        Category = $category
        BugId = $bugId
        RegressionFor = $regressionFor
        Passed = $testOk
        ExitCode = $exitCode
        ElapsedSeconds = $elapsed
        InstallDir = $installInfo.InstallDir
        AssertionsTotal = $assertResult.Total
        AssertionsMet = $assertResult.Met
        Failures = @($assertResult.Failures)
    }
}

# ----------------------------------------------------------------
# Optional: queued build first
# ----------------------------------------------------------------

if ($Build) {
    if (-not $Session) {
        $Session = "smoke-{0:yyyyMMddHHmmss}" -f (Get-Date)
    }
    Write-Info ("Running queued build (session={0}, target=client)" -f $Session)
    $buildScript = Join-Path $ProjectRoot "devtools\build-session.ps1"
    if (-not (Test-Path -LiteralPath $buildScript)) {
        throw "Cannot find queued build wrapper: $buildScript"
    }
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $buildScript -Session $Session -Target client
    $rc = $LASTEXITCODE
    if ($rc -ne 0) {
        Write-Fail ("Build failed with exit code {0}. Aborting smoke run." -f $rc)
        exit $rc
    }
}

# ----------------------------------------------------------------
# Discover + select
# ----------------------------------------------------------------

$all = Get-SmokeTests -Dir $TestsDir
if ($all.Count -eq 0) {
    throw "No tests found in $TestsDir"
}

$selected = Select-SmokeTests `
    -All $all `
    -Names $Test `
    -Tags $Tag `
    -UseAutoSelect:$AutoSelect `
    -AutoSelectBase $MergeBase

if ($selected.Count -eq 0) {
    Write-Warn "No tests matched the selection criteria."
    exit 0
}

Write-Info ("Selected {0} of {1} tests." -f $selected.Count, $all.Count)
foreach ($t in $selected) {
    Write-Info ("  - {0}" -f $t.Name)
}

# ----------------------------------------------------------------
# Run
# ----------------------------------------------------------------

$results = @()
foreach ($t in $selected) {
    $r = Invoke-SmokeTest `
        -Test $t `
        -ExistingInstall $Install `
        -KeepOnSuccess:$Keep `
        -TimeoutOverride $Timeout `
        -VerboseEval:$VerboseAssertions `
        -BinaryOverride $SourceBinary `
        -RomOverride $SourceRom
    $results += $r
}

# ----------------------------------------------------------------
# Summary
# ----------------------------------------------------------------

Write-Host ""
Write-Host "  Summary" -ForegroundColor Cyan
Write-Host "  -------" -ForegroundColor Cyan
$passCount = 0
$failCount = 0
foreach ($r in $results) {
    $tag = if ($r.Passed) { "PASS" } else { "FAIL" }
    $color = if ($r.Passed) { "Green" } else { "Red" }
    $bugSuffix = ""
    if ($r.BugId) { $bugSuffix = " [{0}]" -f $r.BugId }
    Write-Host ("  [{0}] {1}{2} ({3:N1}s) assertions={4}/{5}" -f `
        $tag, $r.Name, $bugSuffix, $r.ElapsedSeconds, $r.AssertionsMet, $r.AssertionsTotal) -ForegroundColor $color
    if ($r.Passed) { $passCount++ } else { $failCount++ }
}

Write-Host ""
Write-Host ("  Total: {0} pass, {1} fail" -f $passCount, $failCount) -ForegroundColor $(if ($failCount -eq 0) { "Green" } else { "Red" })

# Write a machine-readable result file alongside the latest run
if (-not (Test-Path -LiteralPath $RunRoot)) {
    New-Item -ItemType Directory -Path $RunRoot -Force | Out-Null
}
$ResultsFile = Join-Path $RunRoot ("results-{0:yyyyMMddTHHmmssZ}.json" -f ((Get-Date).ToUniversalTime()))
$results | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $ResultsFile -Encoding UTF8
Write-Info ("Results written to: {0}" -f $ResultsFile)

exit $(if ($failCount -eq 0) { 0 } else { 1 })
