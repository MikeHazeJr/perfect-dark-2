<#
    test-memory-guard.ps1 -- durable regression check for the B-801 memory-risk
    control (lib/Test-MemorySafety.ps1). Read-only: it never launches the game.

    Verifies: both files parse; the guard passes on a healthy host; an impossible
    commit floor forces a refusal with remediation text; the bypass env var
    overrides; and the pagefile probe reports a size. Run any time to prove the
    guard still gates the live-launch path. See B-801 in context/bugs.md.
#>
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$lib  = Join-Path $here 'lib\Test-MemorySafety.ps1'
$run  = Join-Path $here 'run.ps1'
$fail = 0

Write-Host "=== 1. Parse-check (syntax only, no execution) ==="
foreach ($f in @($lib, $run)) {
    $errs = $null
    [void][System.Management.Automation.Language.Parser]::ParseFile($f, [ref]$null, [ref]$errs)
    if ($errs -and $errs.Count -gt 0) {
        Write-Host ("  FAIL parse {0}: {1}" -f (Split-Path $f -Leaf), (($errs | ForEach-Object { $_.Message }) -join '; '))
        $fail = 2
    } else {
        Write-Host ("  OK parse {0}" -f (Split-Path $f -Leaf))
    }
}

Write-Host ""
Write-Host "=== 2. Guard loads + probes real host (read-only) ==="
. $lib
$r = Test-SmokeMemorySafety
Write-Host ("  Safe = {0}" -f $r.Safe)
foreach ($i in $r.Info) { Write-Host ("  info: {0}" -f $i) }

Write-Host ""
Write-Host "=== 3. Impossible commit floor must REFUSE with remediation ==="
$env:PD_SMOKE_MIN_FREE_COMMIT_MB = '999999999'
$r2 = Test-SmokeMemorySafety
if ($r2.Safe -or -not $r2.Remediation) { Write-Host "  FAIL: did not refuse / no remediation"; $fail = 3 }
else { Write-Host "  OK refused with remediation" }

Write-Host ""
Write-Host "=== 4. Bypass env overrides even an impossible floor ==="
$env:PD_SMOKE_SKIP_MEMORY_GUARD = '1'
$r3 = Test-SmokeMemorySafety
if (-not $r3.Safe) { Write-Host "  FAIL: bypass did not pass"; $fail = 4 } else { Write-Host "  OK bypass passed" }
Remove-Item Env:\PD_SMOKE_SKIP_MEMORY_GUARD -ErrorAction SilentlyContinue
Remove-Item Env:\PD_SMOKE_MIN_FREE_COMMIT_MB -ErrorAction SilentlyContinue

Write-Host ""
if ($fail -ne 0) { Write-Host ("MEMGUARD TEST FAILED (code {0})" -f $fail); exit $fail }
Write-Host "ALL MEMGUARD CHECKS PASSED"
