#Requires -Version 7.0
<#
Milestone 3 of the build queue spec: the worker in the foreground against a
local bare repository standing in for GitHub. Real clean builds (ccache off),
real pd-tests, a real push to the local remote; nothing reaches GitHub.

  1. a trivial patch (a comment) ends Landed with a commit carrying both trailers;
  2. a patch with a compile error ends Failed with the error line in its report;
  3. a patch whose lines changed on the remote after submit ends Rejected;
  and after each job the jobs folder is empty and builds.jsonl has step times.
Holds the coordination hub's build slot during each build, as the service does.
#>
param(
    [string] $Source = 'C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike',
    [switch] $KeepFixture
)
$ErrorActionPreference = 'Stop'
$pwsh = (Get-Process -Id $PID).Path
$here = Split-Path -Parent $PSScriptRoot
Import-Module (Join-Path $here 'Pd2Queue.psm1') -Force
$git = Get-Pd2qGitExe
$cli = Join-Path $here 'pd2q.ps1'
$service = Join-Path $here 'Queue-Service.ps1'
$root = 'C:\PD2\m3-' + [guid]::NewGuid().ToString('N').Substring(0, 8)
$remote = Join-Path $root 'test-remote.git'
$checks = New-Object 'Collections.Generic.List[string]'
function Check([bool] $Ok, [string] $Label) { if (-not $Ok) { throw "Check failed: $Label" }; $checks.Add($Label); "  ok  $Label" }
function Q([string[]] $Arguments) {
    $ErrorActionPreference = 'Continue'
    $out = & $pwsh -NoProfile -File $cli @Arguments 2>&1 | ForEach-Object { "$_" } | Out-String
    return [pscustomobject]@{ Code = $LASTEXITCODE; Out = $out }
}
function LeaseOf([string] $Text) { if ($Text -match 'lease (\d+) on') { return $Matches[1] }; throw "no lease in: $Text" }
function JobOf([string] $Text) { $m = [regex]::Match($Text, 'q\d+-\d{8}'); if (-not $m.Success) { throw "no job in: $Text" }; return $m.Value }
function LastEvent([string] $Job) { (Get-Content -LiteralPath (Join-Path $root "queue\journal\$Job.jsonl") | Select-Object -Last 1 | ConvertFrom-Json).event }
function Edit-Line([string] $File, [scriptblock] $Change) {
    $t = [IO.File]::ReadAllText($File); [IO.File]::WriteAllText($File, (& $Change $t))
}

$sleeper = $null
$env:PD2Q_ROOT = $root
$env:PD2Q_SUBMODULE_SOURCE = Join-Path $Source '.git\modules'
try {
    $t0 = Get-Date
    & $git clone --quiet --bare --no-hardlinks --branch dev --single-branch $Source $remote
    if ($LASTEXITCODE -ne 0) { throw 'bare remote clone failed' }
    $r = & $pwsh -NoProfile -File $service -Setup -Remote $remote 2>&1 | Out-String
    Check ($LASTEXITCODE -eq 0 -and $r -match 'layout ready') "setup made the layout and dev clone`n$r"
    Check ((& $git -C (Join-Path $root 'dev') status --porcelain) -eq $null) 'the dev clone is clean'
    "setup took $([int]((Get-Date) - $t0).TotalSeconds) s"

    $sleeper = Start-Process -FilePath $pwsh -ArgumentList @('-NoProfile', '-Command', 'Start-Sleep 3600') -PassThru -WindowStyle Hidden

    # 1. A trivial patch: a comment in a C file the client and tests compile.
    $r = Q @('claim', 'T-M3-001', '-Session', 'm3-a', '-OwnerPid', $sleeper.Id); $l1 = LeaseOf $r.Out
    $f = (Q @('edit', 'port/src/gdl_span.c', '-Session', 'm3-a')).Out.Trim()
    Edit-Line $f { param($t) "/* pd2q Milestone 3 trivial patch */`n" + $t }
    $body1 = Join-Path $root 'body-1.txt'
    [IO.File]::WriteAllText($body1, "A comment at the top of gdl_span.c, so the queue builds, tests and lands one real patch.`n")
    $r = Q @('submit', 'T-M3-001', '-Lease', $l1, '-Title', 'Tooling - T-M3-001: Add a build queue test comment', '-Body', $body1, '-CoAuthor', 'Queue Test <noreply@example.invalid>')
    Check ($r.Code -eq 0) "job 1 submitted`n$($r.Out)"; $j1 = JobOf $r.Out

    # 2. A compile error in the same small file.
    $r = Q @('claim', 'T-M3-002', '-Session', 'm3-b', '-OwnerPid', $sleeper.Id); $l2 = LeaseOf $r.Out
    $f = (Q @('edit', 'port/src/gdl_span.c', '-Session', 'm3-b')).Out.Trim()
    Edit-Line $f { param($t) $t + "`nthis line is not C and breaks the build;`n" }
    $r = Q @('submit', 'T-M3-002', '-Lease', $l2, '-Title', 'Tooling - T-M3-002: Break the build on purpose')
    Check ($r.Code -eq 0) 'job 2 submitted'; $j2 = JobOf $r.Out

    # 3. A patch to README.md, then the same lines change on the remote.
    $r = Q @('claim', 'T-M3-003', '-Session', 'm3-c', '-OwnerPid', $sleeper.Id); $l3 = LeaseOf $r.Out
    $f = (Q @('edit', 'README.md', '-Session', 'm3-c')).Out.Trim()
    Edit-Line $f { param($t) $t -replace '(?m)\A(.*)$', '$1 (pd2q M3 session edit)' }
    $r = Q @('submit', 'T-M3-003', '-Lease', $l3, '-Title', 'Tooling - T-M3-003: Edit the README first line')
    Check ($r.Code -eq 0) 'job 3 submitted'; $j3 = JobOf $r.Out
    $other = Join-Path $root 'other'
    & $git clone --quiet --branch dev $remote $other
    & $git -C $other config user.name m3; & $git -C $other config user.email m3@localhost
    Edit-Line (Join-Path $other 'README.md') { param($t) $t -replace '(?m)\A(.*)$', '$1 (someone else changed this line)' }
    & $git -C $other commit -q -am 'Someone else edits the README first line'
    & $git -C $other push -q origin dev
    Check ($LASTEXITCODE -eq 0) 'a conflicting change reached the remote after submit'

    # Run the worker until the queue is empty.
    $t1 = Get-Date
    $log = Join-Path $root 'service-run.log'
    & $pwsh -NoProfile -File $service -Foreground -Remote $remote -IdleExitSeconds 3 -HubSession 'pd2-queue-m3test' *> $log
    "worker ran $([int]((Get-Date) - $t1).TotalSeconds) s"

    $why1 = "`n--- report ---`n" + (Get-Content -LiteralPath (Join-Path $root "reports\$j1.md") -Raw -ErrorAction SilentlyContinue) +
        "`n--- service.log ---`n" + ((Get-Content -LiteralPath (Join-Path $root 'queue\service.log') -Tail 15 -ErrorAction SilentlyContinue) -join "`n") +
        "`n--- worker output ---`n" + (Get-Content -LiteralPath $log -Raw -ErrorAction SilentlyContinue)
    Check ((LastEvent $j1) -eq 'landed') "job 1 landed$why1"
    $report1 = Get-Content -LiteralPath (Join-Path $root "reports\$j1.md") -Raw
    Check ($report1 -match '(?m)^- pd-tests: All tests passed \(\d+ assertions in \d+ test cases\)$') "job 1's report has the full pd-tests summary`n$report1"
    $body = & $git -C $remote log -1 --format=%B dev~1 2>$null
    $landedBody = (& $git -C $remote log --grep "PD2-Queue-Job: $j1" --format=%B dev) -join "`n"
    Check ($landedBody -match "(?m)^PD2-Issue: T-M3-001$" -and $landedBody -match "(?m)^PD2-Queue-Job: $j1$") 'the landed commit carries both trailers'
    Check ($landedBody -match '(?m)^A comment at the top of gdl_span\.c, so the queue builds' -and $landedBody -match '(?m)^Refs: T-M3-001$' -and
        ($landedBody.TrimEnd() -split "`n")[-1] -eq 'Co-Authored-By: Queue Test <noreply@example.invalid>') "the landed commit has the submitted body, Refs, and the co-author trailer last`n$landedBody"
    $landedFiles = @(& $git -C $remote show --name-only --format= (& $git -C $remote log --grep "PD2-Queue-Job: $j1" --format=%H dev) | Where-Object { $_ })
    Check ($landedFiles.Count -eq 1 -and $landedFiles[0] -eq 'port/src/gdl_span.c') "the landed commit is the patch: $($landedFiles -join ', ')"
    Check ((LastEvent $j2) -eq 'failed') 'job 2 failed'
    $report2 = Get-Content -LiteralPath (Join-Path $root "reports\$j2.md") -Raw
    Check ($report2 -match 'gdl_span\.c' -and $report2 -match 'error') "job 2's report holds the compile error line`n$report2"
    Check ((LastEvent $j3) -eq 'rejected') 'job 3 was rejected: its patch no longer applies'
    Check ((Get-Content -LiteralPath (Join-Path $root "reports\$j3.md") -Raw) -match 'README\.md') "job 3's report names the conflicting file"
    Check (@(Get-ChildItem -LiteralPath (Join-Path $root 'jobs') -Force).Count -eq 0) 'the jobs folder is empty after the jobs'
    $builds = @(Get-Content -LiteralPath (Join-Path $root 'queue\builds.jsonl') | ForEach-Object { $_ | ConvertFrom-Json })
    Check ($builds.Count -ge 2 -and $builds[0].seconds.build -gt 0 -and $builds[0].seconds.tests -gt 0) ('builds.jsonl has step times: ' + ($builds | ForEach-Object { "$($_.job) $($_.result) build $($_.seconds.build) s tests $($_.seconds.tests) s total $($_.total) s slow $($_.slow)" }) -join '; ')
    $r = Q @('check', 'T-M3-001')
    Check ($r.Out -match 'finished' -and $r.Out -match 'landed') 'pd2q check shows job 1 landed'
    $r = Q @('claim', 'T-M3-001', '-Session', 'm3-d', '-OwnerPid', $sleeper.Id)
    Check ($r.Code -ne 0 -and $r.Out -match 'already landed') 'a claim on the landed issue refuses'
    Check ((& $git -C (Join-Path $root 'dev') rev-parse HEAD) -eq (& $git -C $remote rev-parse dev)) 'the dev clone was reset to the new tip'
} finally {
    if ($sleeper) { try { Stop-Process -Id $sleeper.Id -Force } catch { } }
    Remove-Item Env:PD2Q_ROOT, Env:PD2Q_SUBMODULE_SOURCE -ErrorAction SilentlyContinue
    if (-not $KeepFixture -and (Test-Path -LiteralPath $root)) {
        Get-ChildItem -LiteralPath $root -Recurse -Force -File -ErrorAction SilentlyContinue | ForEach-Object { $_.Attributes = 'Normal' }
        Remove-Item -LiteralPath $root -Recurse -Force
    }
}
"PASS: $($checks.Count) worker checks."
exit 0
