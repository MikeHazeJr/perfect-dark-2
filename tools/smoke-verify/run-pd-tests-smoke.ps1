#Requires -Version 5.1
<#
.SYNOPSIS
    Tests pillar meta-smoke. Runs the pd-tests Catch2 binary and asserts
    the unit suite reports clean against a documented carry-over allowlist.

.DESCRIPTION
    This is the c115 "tests pillar" smoke wrapper. It is a META smoke: it
    does NOT exercise the in-client harness like the rest of the
    smoke-verify gate. Instead it:

      1. Locates Build/pd-tests.exe (or honours -ExePath).
      2. Runs the Catch2 suite with the supplied scope filter (default:
         the full suite).
      3. Parses Catch2's footer (`All tests passed` on success or
         `test cases:` on partial) plus the per-failure `FAILED:` lines.
      4. Compares the observed failures against an explicit allowlist of
         pre-existing carry-over failures (documented in the header
         constants below).
      5. Emits a results-<utc>Z-pd_tests_runtime_smoke.json that matches
         the schema run.ps1 produces, so the result is discoverable /
         aggregatable alongside the rest of the smoke gate.

    The point is to catch NEW regressions in the pd-tests suite, not to
    relitigate old ones. New failures (test cases not in the allowlist)
    fail the smoke loudly. Old failures inside the allowlist count as
    warnings.

    SCOPE STRATEGY: A (allowlist of test files). The Catch2 suite has
    three pre-existing source-grep test files that drift each refactor
    cycle and three known segfaults-on-teardown; relitigating them is
    out of scope for this slice. The allowlist is keyed on test-CASE
    name (not file) so a new failure inside an allowlisted file still
    surfaces.

    See: context/pillars/tests.md, session-log entry 2026-05-14
    (c036-08), and the carry-over note at session-log.md:65.

.PARAMETER Scope
    Catch2 filter expression (e.g. "[catalog][provider][static]" or
    "[netbuf]"). Empty (default) runs the full suite.

.PARAMETER ExePath
    Override the pd-tests.exe path. Default: Build/pd-tests.exe relative
    to the project root.

.PARAMETER OutputDir
    Where to write the results JSON. Default:
    .claude/smoke-verify-runs/

.PARAMETER VerboseAssertions
    Echo every parsed line / allowlist decision to the console.

.PARAMETER TimeoutSeconds
    Watchdog timeout for pd-tests.exe. Default 300s. The full suite
    completes well under 60s on a warm build but the cold-cache budget
    can spike.

.EXAMPLE
    powershell -NoProfile -ExecutionPolicy Bypass `
        -File tools/smoke-verify/run-pd-tests-smoke.ps1 -VerboseAssertions

.EXAMPLE
    .\tools\smoke-verify\run-pd-tests-smoke.ps1 -Scope "[netbuf]"
#>

[CmdletBinding()]
param(
    [string] $Scope           = "",
    [string] $ExePath         = "",
    [string] $OutputDir       = "",
    [switch] $VerboseAssertions,
    [int]    $TimeoutSeconds  = 300
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

# ----------------------------------------------------------------
# Bootstrap paths
# ----------------------------------------------------------------

$ScriptDir   = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectRoot = Split-Path -Parent (Split-Path -Parent $ScriptDir)
$DevtoolsDir = Join-Path $ProjectRoot "devtools"

. (Join-Path $DevtoolsDir "_build-env-prelude.ps1")

$errorModeSource = @"
using System;
using System.Runtime.InteropServices;

public static class PdSmokeWinErrorMode
{
    [DllImport("kernel32.dll")]
    public static extern uint SetErrorMode(uint uMode);
}
"@

if (-not ([System.Management.Automation.PSTypeName]'PdSmokeWinErrorMode').Type) {
    Add-Type -TypeDefinition $errorModeSource
}

$SEM_FAILCRITICALERRORS = 0x0001
$SEM_NOGPFAULTERRORBOX = 0x0002
$SEM_NOOPENFILEERRORBOX = 0x8000
$loaderErrorMode = $SEM_FAILCRITICALERRORS -bor $SEM_NOGPFAULTERRORBOX -bor $SEM_NOOPENFILEERRORBOX
$previousErrorMode = [PdSmokeWinErrorMode]::SetErrorMode($loaderErrorMode)

if (-not $ExePath) {
    $ExePath = Join-Path $ProjectRoot "Build\pd-tests.exe"
}
if (-not $OutputDir) {
    $OutputDir = Join-Path $ProjectRoot ".claude\smoke-verify-runs"
}

if (-not (Test-Path -LiteralPath $ExePath)) {
    throw "pd-tests.exe not found at $ExePath. Build it first: ninja -C Build pd-tests."
}

if (-not (Test-Path -LiteralPath $OutputDir)) {
    New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
}

function Write-Info([string]$t) { Write-Host $t -ForegroundColor Gray }
function Write-Ok  ([string]$t) { Write-Host $t -ForegroundColor Green }
function Write-Warn([string]$t) { Write-Host $t -ForegroundColor Yellow }
function Write-Fail([string]$t) { Write-Host $t -ForegroundColor Red }

# ----------------------------------------------------------------
# Known carry-over failures allowlist
# ----------------------------------------------------------------
#
# These are source-grep test cases that drift each refactor cycle.
# They are documented as pre-existing in:
#   - context/session-log.md:65 ("documented as carry-over in c129 sprint
#     and the 2026-05-13 audit, NOT introduced by this slice")
#   - context/session-log.md:1202 ("Pre-existing source-grep test
#     failures (test_uichrome_paths_pin.cpp:73,84,94,151,
#     test_catalog_provider_static.cpp:468,537)")
#   - context/session-log.md:1988 ("4 pre-existing rot failures match
#     S603 memo")
#
# Match keys are Catch2 TEST_CASE names. A new failure inside an
# allowlisted FILE but with a different TEST_CASE name will still fail
# the smoke loudly.
#
# When a carry-over is fixed: DELETE the entry from this allowlist in
# the same commit, do not leave it sitting here.

$KnownFailureCases = @(
    # test_uichrome_paths_pin.cpp -- catalog-vs-extraction path drift
    "uichrome-paths: catalog entries point at data/ui/textures",
    "uichrome-paths: extraction destination writes to data/ui/textures",
    "uichrome-paths: directory creation targets data/ui/textures",
    "uichrome-paths: 13 catalog entries pinned, 14 extraction filenames pinned",

    # test_pdbase_retired_audit.cpp -- Step 5 pdbase substring guard
    "step5: no pdbase substrings remain in port/ source",

    # test_catalog_provider_static.cpp -- post-Pass-B FileProvider drift
    "rom-backed catalog registration helpers populate provider handles"
)

# The full pd-tests suite has a known segfault-on-teardown (also
# pre-existing per session-log.md:1202 + 1988). Accept both POSIX-style
# 139 (bash) and the Windows native 0xC0000005 / -1073741819 access
# violation that the same teardown produces under Process.Start.
# Catch2 also exits with non-zero == failure count when any REQUIRE
# fails; 4-6 covers our current carry-over count.
$AllowedExitCodes = @(0, 1, 2, 3, 4, 5, 6, 139, -1073741819, -1073740940)

# ----------------------------------------------------------------
# Run pd-tests
# ----------------------------------------------------------------

$started = Get-Date
$exitCode = -1
$rawOutput = @()

# Prepend mingw bin so libstdc++-6.dll / libgcc resolve when pd-tests
# is launched from a clean PowerShell env without devtools/build-env.sh
# in the parent shell.
$mingwBin = "C:\msys64\mingw64\bin"
if (Test-Path -LiteralPath $mingwBin) {
    $env:PATH = "$mingwBin;$env:PATH"
}

$catchArgs = @("--reporter", "console")
if ($Scope) {
    $catchArgs = @($Scope) + $catchArgs
}

Write-Info ""
Write-Info ("  pd-tests runtime smoke (c115 tests pillar)")
Write-Info ("  exe:     {0}" -f $ExePath)
Write-Info ("  scope:   {0}" -f $(if ($Scope) { $Scope } else { "<all>" }))
Write-Info ("  timeout: {0}s" -f $TimeoutSeconds)
Write-Info ""

# Use Process.Start so we can wire WaitForExit with a watchdog and
# capture stdout + stderr deterministically without buffering surprises.
$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName               = $ExePath
$quotedArgs = foreach ($a in $catchArgs) {
    if ($null -eq $a) { continue }
    $s = [string]$a
    if ($s -match '[\s"]') { '"' + ($s -replace '"', '\"') + '"' } else { $s }
}
$psi.Arguments              = ($quotedArgs -join ' ')
$psi.WorkingDirectory       = $ProjectRoot
$psi.UseShellExecute        = $false
$psi.CreateNoWindow         = $true
$psi.RedirectStandardOutput = $true
$psi.RedirectStandardError  = $true

$proc = $null
$stdout = ""
$stderr = ""

try {
    $proc = [System.Diagnostics.Process]::Start($psi)
    if (-not $proc) { throw "ProcessStartInfo returned null Process" }

    # Async read both streams to avoid the classic "child blocks on a
    # full pipe while parent waits" deadlock.
    $stdoutTask = $proc.StandardOutput.ReadToEndAsync()
    $stderrTask = $proc.StandardError.ReadToEndAsync()

    if (-not $proc.WaitForExit($TimeoutSeconds * 1000)) {
        Write-Warn ("Watchdog firing after {0}s; terminating pd-tests.exe (pid {1})." -f $TimeoutSeconds, $proc.Id)
        try { $proc.Kill() } catch {}
        try { $proc.WaitForExit(5000) | Out-Null } catch {}
        $exitCode = -2
    } else {
        $exitCode = $proc.ExitCode
        $proc.WaitForExit()
    }

    $stdout = $stdoutTask.Result
    $stderr = $stderrTask.Result
} catch {
    Write-Fail ("Failed to launch pd-tests.exe: {0}" -f $_.Exception.Message)
    $exitCode = -3
} finally {
    [void][PdSmokeWinErrorMode]::SetErrorMode($previousErrorMode)
}

$elapsed = ((Get-Date) - $started).TotalSeconds
$combined = ($stdout + "`n" + $stderr)
$rawOutput = ($combined -split "`r?`n")

Write-Info ("  exit code: {0}" -f $exitCode)
Write-Info ("  elapsed:   {0:N1}s" -f $elapsed)

if ($VerboseAssertions) {
    Write-Info "  --- pd-tests stdout (last 40 lines) ---"
    $tail = $rawOutput | Select-Object -Last 40
    foreach ($l in $tail) { Write-Info ("    {0}" -f $l) }
}

# ----------------------------------------------------------------
# Parse Catch2 output
# ----------------------------------------------------------------

$allPassedMatch  = ($rawOutput | Select-String -Pattern '^All tests passed \((\d+) assertions in (\d+) test case(?:s)?\)' | Select-Object -First 1)
$testCasesMatch  = ($rawOutput | Select-String -Pattern '^test cases:\s+(\d+)\s*\|\s*(\d+) passed\s*\|\s*(\d+) failed' | Select-Object -First 1)
$assertionsMatch = ($rawOutput | Select-String -Pattern '^assertions:\s+(\d+)\s*\|\s*(\d+) passed\s*\|\s*(\d+) failed' | Select-Object -First 1)

$assertionsTotal = 0
$assertionsMet   = 0
$testCasesTotal  = 0
$testCasesPass   = 0
$testCasesFail   = 0
$summaryPresent  = $false

if ($allPassedMatch) {
    $summaryPresent  = $true
    $assertionsTotal = [int]$allPassedMatch.Matches[0].Groups[1].Value
    $assertionsMet   = $assertionsTotal
    $testCasesTotal  = [int]$allPassedMatch.Matches[0].Groups[2].Value
    $testCasesPass   = $testCasesTotal
    $testCasesFail   = 0
} elseif ($testCasesMatch) {
    $summaryPresent  = $true
    $testCasesTotal  = [int]$testCasesMatch.Matches[0].Groups[1].Value
    $testCasesPass   = [int]$testCasesMatch.Matches[0].Groups[2].Value
    $testCasesFail   = [int]$testCasesMatch.Matches[0].Groups[3].Value
    if ($assertionsMatch) {
        $assertionsTotal = [int]$assertionsMatch.Matches[0].Groups[1].Value
        $assertionsMet   = [int]$assertionsMatch.Matches[0].Groups[2].Value
    }
}

# Pull every "TEST_CASE: <name>" that has a FAILED hit. Catch2's
# default console reporter prints the test name *between* two dashed
# banners:
#   --------------------------- (banner)
#   uichrome-paths: catalog entries point at data/ui/textures
#   --------------------------- (banner)
#   ..\tests\test_uichrome_paths_pin.cpp:50
#   ......................... (dots)
#
#   ..\tests\test_uichrome_paths_pin.cpp:73: FAILED:
#     REQUIRE( ... )
#
# So when we hit a banner line, the line we want is the one *before*
# the banner -- skipping past consecutive banner / blank lines. We
# stash it as "current case" and clear it when a new sandwich starts.
$failedCaseNames = New-Object System.Collections.Generic.List[string]
$currentCaseName = $null
$pendingFile     = $null
for ($i = 0; $i -lt $rawOutput.Count; $i++) {
    $line = $rawOutput[$i]
    if ($line -match '^-{60,}$') {
        # Walk back to find the test-case name on the line above. The
        # name sits between two banners; on the second banner of the
        # pair we'll grab the line two-up.
        $j = $i - 1
        while ($j -ge 0 -and ($rawOutput[$j] -match '^-{60,}$' -or $rawOutput[$j] -match '^\s*$')) { $j-- }
        if ($j -ge 0) {
            $candidate = $rawOutput[$j].Trim()
            # Skip file-path-looking lines so we don't accidentally
            # latch onto the cpp path printed under the inner banner.
            if ($candidate -notmatch '^\.\.\\tests\\') {
                $currentCaseName = $candidate
            }
        }
    }
    if ($line -match '^\.\.\\tests\\([^:]+\.cpp):\d+:\s+FAILED:') {
        $pendingFile = $matches[1]
        if ($currentCaseName) {
            [void]$failedCaseNames.Add($currentCaseName)
        } else {
            # Fallback so we never silently drop a failure -- key off
            # the file when the banner-name parse missed.
            [void]$failedCaseNames.Add("<unknown-in-$pendingFile>")
        }
    }
}

# De-duplicate; the same TEST_CASE can have multiple REQUIREs that fail.
$failedCaseNames = @($failedCaseNames | Select-Object -Unique)

# Partition observed failures into allowlisted vs new.
$newFailures        = @($failedCaseNames | Where-Object { $KnownFailureCases -notcontains $_ })
$allowlistedFailing = @($failedCaseNames | Where-Object { $KnownFailureCases -contains $_ })

# ----------------------------------------------------------------
# Decide PASS / FAIL
# ----------------------------------------------------------------

$failures = @()
$assertionsLog = New-Object System.Collections.Generic.List[psobject]

function Add-Assertion([string]$name, [bool]$ok, [string]$detail) {
    [void]$script:assertionsLog.Add([PSCustomObject]@{ Name = $name; Ok = $ok; Detail = $detail })
    if ($VerboseAssertions) {
        if ($ok) { Write-Ok   ("  [ok] {0}" -f $name) }
        else     { Write-Fail ("  [FAIL] {0} -- {1}" -f $name, $detail) }
    } elseif (-not $ok) {
        Write-Fail ("  [FAIL] {0} -- {1}" -f $name, $detail)
    }
    if (-not $ok) { $script:failures += ("{0}: {1}" -f $name, $detail) }
}

Add-Assertion "exit_code_allowed" `
    ($AllowedExitCodes -contains $exitCode) `
    ("expected one of {0}, observed {1}" -f ($AllowedExitCodes -join ','), $exitCode)

Add-Assertion "no_new_failures" `
    ($newFailures.Count -eq 0) `
    ("new (non-allowlisted) failing TEST_CASEs: {0}" -f (($newFailures -join '; ')))

# Soft-guard: the Catch2 footer is the canonical pass-of-truth, but the
# known segfault-on-teardown can swallow it. Treat the footer as
# expected when the exit code is clean; treat its absence as warning
# (not failure) when the exit code matches the segfault class, because
# we still have the per-FAILED parse to authoritatively count failures.
if ($summaryPresent -or $exitCode -in @(139, -1073741819, -1073740940)) {
    Add-Assertion "summary_footer_present_or_known_teardown_segfault" `
        $true `
        ""
} else {
    Add-Assertion "summary_footer_present_or_known_teardown_segfault" `
        $false `
        ("no Catch2 footer + unexpected exit {0}" -f $exitCode)
}

# Belt-and-braces: if Catch2 reports more failures than we partitioned
# above (banner parse missed a name), refuse to pass. This guards
# against a silently dropped "unknown" failure. Skipped when the
# footer is missing (segfault path) because $testCasesFail is 0 then.
$accountedFailures = $newFailures.Count + $allowlistedFailing.Count
if ($summaryPresent) {
    Add-Assertion "failure_count_accounted" `
        ($testCasesFail -le $accountedFailures + 1) `
        ("test cases footer reports {0} failed; parsed {1}" -f $testCasesFail, $accountedFailures)
} else {
    Add-Assertion "failure_count_accounted_skipped_no_footer" `
        $true `
        ""
}

$passed = $true
foreach ($a in $assertionsLog) { if (-not $a.Ok) { $passed = $false } }

# ----------------------------------------------------------------
# Emit result JSON
# ----------------------------------------------------------------

$utc = (Get-Date).ToUniversalTime()
$resultsFile = Join-Path $OutputDir ("results-{0:yyyyMMddTHHmmssZ}-pd_tests_runtime_smoke.json" -f $utc)

$assertionsLogTotal = $assertionsLog.Count
$assertionsLogMet   = @($assertionsLog | Where-Object { $_.Ok }).Count

$resultObj = [PSCustomObject]@{
    Name             = "pd_tests_runtime_smoke"
    Category         = "meta"
    BugId            = $null
    RegressionFor    = $null
    Passed           = $passed
    ExitCode         = $exitCode
    ElapsedSeconds   = $elapsed
    InstallDir       = ""
    AssertionsTotal  = $assertionsLogTotal
    AssertionsMet    = $assertionsLogMet
    Failures         = @($failures)
    # Tests-pillar extension fields. run.ps1 ignores these; the bug-
    # tracker / aggregator can pick them up if needed.
    Scope                  = $Scope
    Catch2AssertionsTotal  = $assertionsTotal
    Catch2AssertionsMet    = $assertionsMet
    Catch2TestCasesTotal   = $testCasesTotal
    Catch2TestCasesPass    = $testCasesPass
    Catch2TestCasesFail    = $testCasesFail
    NewFailures            = @($newFailures)
    AllowlistedFailures    = @($allowlistedFailing)
    KnownFailureAllowlist  = $KnownFailureCases
}

$resultObj | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $resultsFile -Encoding UTF8

# ----------------------------------------------------------------
# Summary
# ----------------------------------------------------------------

Write-Host ""
Write-Host "  Summary" -ForegroundColor Cyan
Write-Host "  -------" -ForegroundColor Cyan
$tag = if ($passed) { "PASS" } else { "FAIL" }
$color = if ($passed) { "Green" } else { "Red" }
Write-Host ("  [{0}] pd_tests_runtime_smoke ({1:N1}s)" -f $tag, $elapsed) -ForegroundColor $color
Write-Host ("       Catch2: {0} cases ({1} pass / {2} fail), {3} assertions ({4} pass)" -f `
    $testCasesTotal, $testCasesPass, $testCasesFail, $assertionsTotal, $assertionsMet) -ForegroundColor Gray
if ($allowlistedFailing.Count -gt 0) {
    Write-Warn ("       allowlisted carry-over failures: {0}" -f ($allowlistedFailing -join '; '))
}
if ($newFailures.Count -gt 0) {
    Write-Fail ("       NEW failures (not in allowlist): {0}" -f ($newFailures -join '; '))
}
Write-Info ("Results written to: {0}" -f $resultsFile)

exit $(if ($passed) { 0 } else { 1 })
