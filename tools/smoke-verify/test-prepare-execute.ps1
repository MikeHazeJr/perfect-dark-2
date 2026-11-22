#Requires -Version 5.1
<#
.SYNOPSIS
    T-TOOLING-019: the smoke runner's prepare/execute boundary, end to end,
    without launching a game.

.DESCRIPTION
    Copies run.ps1, its libraries, the storage backend and the two devtools
    files they load into an isolated project under canonical
    .claude/smoke-storage-selftests (storage.py refuses any other project), with
    a fake template install, one fixture and one test definition. Then checks:
      1. option misuse is refused before anything is staged;
      2. -Prepare stages a private copy of the template plus the fixture, hands
         it off and writes a prepared-run record, launching nothing;
      3. a staged file edited after the handoff makes -Execute refuse and the
         install stays active for review;
      4. -Execute -DiscardPrepared adopts a clean prepared install and closes it;
      5. the same record cannot be executed again (one use);
      6. a runner changed after -Prepare is refused.
    Each runner call is a child powershell process, as in real use, so the
    preparer has exited before the executor adopts. Leaves nothing behind.
#>
param([switch] $KeepFixture)
$ErrorActionPreference = 'Stop'
$canonical = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$id = [guid]::NewGuid().ToString('N').Substring(0, 12)
$project = Join-Path $canonical ".claude\smoke-storage-selftests\prepare-execute-$id"
$checks = New-Object 'Collections.Generic.List[string]'
function Check([bool] $Passed, [string] $Label) {
    if (-not $Passed) { throw "Check failed: $Label" }
    $checks.Add($Label)
}
$hostExe = (Get-Process -Id $PID).Path
function Invoke-Runner([string[]] $Arguments) {
    # A child process per step, exactly as a session calls the runner.
    $runner = Join-Path $project 'tools\smoke-verify\run.ps1'
    # Windows PowerShell 5.1 turns a child's stderr into error records; a
    # refusal is an expected outcome here, so collect it as text.
    $ErrorActionPreference = 'Continue'
    $output = & $hostExe -NoProfile -ExecutionPolicy Bypass -File $runner @Arguments 2>&1 |
        ForEach-Object { if ($_ -is [Management.Automation.ErrorRecord]) { $_.Exception.Message } else { [string]$_ } } | Out-String
    return [PSCustomObject]@{ ExitCode = $LASTEXITCODE; Output = $output }
}
function Get-StorageRow([string] $StorageId) {
    $state = Get-Content -LiteralPath (Join-Path $project '.claude\smoke-storage\state.json') -Raw | ConvertFrom-Json
    return $state.installs.$StorageId
}

try {
    foreach ($relative in @('tools\smoke-verify\run.ps1', 'tools\smoke-verify\storage.py',
            'tools\smoke-verify\storage-policy.json', 'devtools\_build-env-prelude.ps1',
            'devtools\smoke-storage-manifest.py')) {
        $destination = Join-Path $project $relative
        [IO.Directory]::CreateDirectory((Split-Path -Parent $destination)) | Out-Null
        Copy-Item -LiteralPath (Join-Path $canonical $relative) -Destination $destination
    }
    $lib = Join-Path $project 'tools\smoke-verify\lib'
    [IO.Directory]::CreateDirectory($lib) | Out-Null
    Get-ChildItem -LiteralPath (Join-Path $canonical 'tools\smoke-verify\lib') -Filter '*.ps1' -File |
        ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $lib $_.Name) }

    # A fake pre-extracted template (never executed) and one fixture file.
    $template = Join-Path $project 'template'
    [IO.Directory]::CreateDirectory((Join-Path $template 'data\unit')) | Out-Null
    [IO.File]::WriteAllBytes((Join-Path $template 'PerfectDark.exe'), [byte[]](1..200 | ForEach-Object { 7 }))
    [IO.File]::WriteAllBytes((Join-Path $template 'pd.ntsc-final.z64'), [byte[]](1..64 | ForEach-Object { 9 }))
    [IO.File]::WriteAllText((Join-Path $template 'data\unit\asset.json'), '{"asset":"template"}')
    [IO.Directory]::CreateDirectory((Join-Path $project 'fixtures')) | Out-Null
    [IO.File]::WriteAllText((Join-Path $project 'fixtures\unit.json'), '{"fixture":"staged by prepare"}')
    $tests = Join-Path $project 'tests'
    [IO.Directory]::CreateDirectory($tests) | Out-Null
    $definition = [ordered]@{
        scenario_name = 'prepared_unit_smoke'; description = 'T-TOOLING-019 boundary unit (never launched)'
        timeout_seconds = 30; boot_args = @('--portable')
        fixtures = @([ordered]@{ src = 'fixtures/unit.json'; dst = 'mods/unit/unit.json' })
        assertions = @()
    }
    [IO.File]::WriteAllText((Join-Path $tests 'prepared_unit_smoke.json'), ($definition | ConvertTo-Json -Depth 6))
    $policy = Join-Path $project 'policy.json'
    [IO.File]::WriteAllText($policy, (@{ schema = 1; min_free_gib = 0; max_full_installs = 4; max_full_install_gib = 0.01
        max_archive_gib = 0.01; peak_run_gib = 0; extra_growth_gib = 0; peak_build_gib = 0 } | ConvertTo-Json))

    # 1. Misuse is refused before anything is staged.
    $r = Invoke-Runner @('-Prepare', '-Execute', 'x.json')
    Check ($r.ExitCode -ne 0 -and $r.Output -match 'separate steps') 'prepare and execute together refused'
    $r = Invoke-Runner @('-Prepare', '-Build', '-Test', 'prepared_unit_smoke')
    Check ($r.ExitCode -ne 0 -and $r.Output -match 'refuse -Build') 'prepare refuses -Build'
    $r = Invoke-Runner @('-Execute', 'x.json', '-Test', 'prepared_unit_smoke')
    Check ($r.ExitCode -ne 0 -and $r.Output -match 'takes its tests') 'execute refuses selection options'
    $r = Invoke-Runner @('-DiscardPrepared')
    Check ($r.ExitCode -ne 0 -and $r.Output -match 'DiscardPrepared') 'discard needs execute'
    Check (-not (Test-Path -LiteralPath (Join-Path $project '.claude\smoke-storage'))) 'refusals created no storage'

    # 2. Prepare: stage, hand off, record; nothing launched.
    $recordA = Join-Path $project 'records\a.json'
    $r = Invoke-Runner @('-Prepare', '-Test', 'prepared_unit_smoke', '-TestsDir', $tests, '-Install', $template,
        '-StoragePolicy', $policy, '-PreparedRecord', $recordA)
    Check ($r.ExitCode -eq 0) ("prepare succeeds`n" + $r.Output)
    Check ($r.Output -match 'nothing launched') 'prepare reports nothing launched'
    $record = Get-Content -LiteralPath $recordA -Raw | ConvertFrom-Json
    Check ($record.schema -eq 'pd2.smoke.prepared-run.v1' -and @($record.tests).Count -eq 1 -and @($record.installs).Count -eq 1) 'record shape'
    $installA = $record.tests[0].install
    Check (Test-Path -LiteralPath (Join-Path $installA.InstallDir 'mods\unit\unit.json')) 'fixture staged at prepare'
    Check ((Get-FileHash -LiteralPath (Join-Path $installA.InstallDir 'PerfectDark.exe')).Hash -eq
        (Get-FileHash -LiteralPath (Join-Path $template 'PerfectDark.exe')).Hash) 'template binary staged'
    $row = Get-StorageRow $installA.StorageId
    Check ($row.status -eq 'active' -and $row.handoff -and $row.handoff.token_sha256.Length -eq 64) 'install handed off, still active'
    Check (-not ($row.handoff.PSObject.Properties.Name -contains 'token')) 'storage keeps only the token digest'
    Check (@(Get-ChildItem -LiteralPath (Join-Path $project '.claude') -Recurse -Filter 'pd-client.log' -File -ErrorAction SilentlyContinue).Count -eq 0) 'no game log: nothing ran'

    # 3. A staged file edited after the handoff: execute refuses, install kept.
    $recordB = Join-Path $project 'records\b.json'
    $r = Invoke-Runner @('-Prepare', '-Test', 'prepared_unit_smoke', '-TestsDir', $tests, '-Install', $template,
        '-StoragePolicy', $policy, '-PreparedRecord', $recordB)
    Check ($r.ExitCode -eq 0) 'second prepare succeeds'
    $installB = (Get-Content -LiteralPath $recordB -Raw | ConvertFrom-Json).tests[0].install
    [IO.File]::WriteAllText((Join-Path $installB.InstallDir 'mods\unit\unit.json'), '{"fixture":"edited after prepare"}')
    $r = Invoke-Runner @('-Execute', $recordB)
    Check ($r.ExitCode -ne 0 -and $r.Output -match 'changed between prepare and execute') 'tampered install refused at execute'
    $rowB = Get-StorageRow $installB.StorageId
    Check ($rowB.status -eq 'active' -and $rowB.handoff) 'tampered install kept for review'

    # 4. Discard a clean prepared install: adopted, closed, nothing launched.
    $r = Invoke-Runner @('-Execute', $recordA, '-DiscardPrepared')
    Check ($r.ExitCode -eq 0) ("discard succeeds`n" + $r.Output)
    $rowA = Get-StorageRow $installA.StorageId
    Check ($rowA.status -eq 'completed' -and -not $rowA.passed -and $rowA.adopted -and
        -not ($rowA.PSObject.Properties.Name -contains 'handoff')) 'discarded install adopted and closed'

    # 5. One use: the same record cannot run again.
    $r = Invoke-Runner @('-Execute', $recordA)
    Check ($r.ExitCode -ne 0 -and $r.Output -match 'Only an active install|no unused prepared handoff') 'record cannot execute twice'

    # 6. A runner changed after prepare is refused.
    $recordC = Join-Path $project 'records\c.json'
    $r = Invoke-Runner @('-Prepare', '-Test', 'prepared_unit_smoke', '-TestsDir', $tests, '-Install', $template,
        '-StoragePolicy', $policy, '-PreparedRecord', $recordC)
    Check ($r.ExitCode -eq 0) 'third prepare succeeds'
    Add-Content -LiteralPath (Join-Path $lib 'Readiness-Output.ps1') -Value '# changed after prepare'
    $r = Invoke-Runner @('-Execute', $recordC)
    Check ($r.ExitCode -ne 0 -and $r.Output -match 'changed since this record was prepared') 'changed runner refused'
    $installC = (Get-Content -LiteralPath $recordC -Raw | ConvertFrom-Json).tests[0].install
    Check ((Get-StorageRow $installC.StorageId).status -eq 'active') 'install of a refused record untouched'
} finally {
    if (-not $KeepFixture -and (Test-Path -LiteralPath $project)) {
        # Only this test's own fixture tree, below canonical .claude/smoke-storage-selftests.
        $root = [IO.Path]::GetFullPath((Join-Path $canonical '.claude\smoke-storage-selftests')) + '\'
        if (-not ([IO.Path]::GetFullPath($project)).StartsWith($root, [StringComparison]::OrdinalIgnoreCase)) { throw 'Refusing to remove outside the self-test root.' }
        Get-ChildItem -LiteralPath $project -Recurse -Force -File | ForEach-Object { $_.Attributes = 'Normal' }
        Remove-Item -LiteralPath $project -Recurse -Force
    }
}
Write-Output ("PASS: {0} prepare/execute boundary checks; no game launched." -f $checks.Count)
# The last runner child is an expected refusal (exit 1). Without this, an
# in-process caller (CI's pwsh step) inherits that $LASTEXITCODE and fails.
exit 0
